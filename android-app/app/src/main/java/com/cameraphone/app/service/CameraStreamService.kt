package com.cameraphone.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import com.cameraphone.app.camera.Camera2Controller
import com.cameraphone.app.camera.HardwareAudioEncoder
import com.cameraphone.app.camera.HardwareVideoEncoder
import com.cameraphone.app.net.CamPhoneServer
import com.cameraphone.app.probe.CapabilityProbe
import com.cameraphone.app.probe.DeviceCapabilityProfile
import com.cameraphone.app.security.CameraBounds
import com.cameraphone.app.security.SecurityGate
import com.cameraphone.app.util.AppLogger
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.util.concurrent.atomic.AtomicBoolean

class CameraStreamService : Service() {

    inner class LocalBinder : Binder() {
        fun getService(): CameraStreamService = this@CameraStreamService
    }

    private val binder = LocalBinder()
    lateinit var securityGate: SecurityGate
        private set

    private var server: CamPhoneServer? = null
    private var cameraController: Camera2Controller? = null
    private var videoEncoder: HardwareVideoEncoder? = null
    private var audioEncoder: HardwareAudioEncoder? = null
    private var nsdManager: NsdManager? = null
    private var nsdListener: NsdManager.RegistrationListener? = null
    private var udpSocket: DatagramSocket? = null
    private val udpRunning = AtomicBoolean(false)
    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private lateinit var capabilities: DeviceCapabilityProfile

    @Volatile
    var usbOnlyMode: Boolean = false
        private set

    @Volatile
    var isConnectionArmed: Boolean = true
        private set

    private data class StreamParams(
        val lensId: String,
        val w: Int,
        val h: Int,
        val fps: Int,
        val codec: String,
        val hdr: Boolean
    )

    @Volatile
    private var lastStreamParams: StreamParams? = null
    @Volatile
    private var streamRequested: Boolean = false

    var onUiStateChange: ((tally: String, streaming: Boolean, activeInfo: String) -> Unit)? = null

    override fun onCreate() {
        super.onCreate()
        AppLogger.info("Service", "CameraStreamService starting on ${Build.MANUFACTURER} ${Build.MODEL} (SDK ${Build.VERSION.SDK_INT})")
        val prefs = getSharedPreferences("camphone_sec", Context.MODE_PRIVATE)
        val savedPin = prefs.getString("pin", null)
        securityGate = SecurityGate(
            initialPin = savedPin,
            onPinChanged = { newPin ->
                prefs.edit().putString("pin", newPin).apply()
            }
        )
        if (savedPin == null) {
            prefs.edit().putString("pin", securityGate.currentPin).apply()
        }

        createNotificationChannel()
        startForeground(1001, buildNotification("Ready for OBS Studio & NVIDIA Broadcast connection (USB & Wi-Fi)"))
        acquirePowerAndWifiLocks()
        capabilities = CapabilityProbe.probeDevice(this)
        AppLogger.info("Service", "Probed ${capabilities.lenses.size} lenses, codecs=${capabilities.codecs}")
        cameraController = Camera2Controller(this)
        // Default to usbOnly = false (0.0.0.0:4748) so BOTH USB Cable (127.0.0.1) and Wi-Fi IP work out-of-the-box!
        restartServer(usbOnly = false)
    }

    private fun acquirePowerAndWifiLocks() {
        try {
            if (wakeLock == null) {
                val pm = applicationContext.getSystemService(Context.POWER_SERVICE) as? PowerManager
                wakeLock = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "CameraPhone::StreamWakeLock")?.apply {
                    setReferenceCounted(false)
                }
            }
            if (wakeLock?.isHeld == false) {
                wakeLock?.acquire()
            }
            if (wifiLock == null) {
                val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
                val lockMode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                } else {
                    @Suppress("DEPRECATION")
                    WifiManager.WIFI_MODE_FULL_HIGH_PERF
                }
                wifiLock = wm?.createWifiLock(lockMode, "CameraPhone::StreamWifiLock")?.apply {
                    setReferenceCounted(false)
                }
            }
            if (wifiLock?.isHeld == false) {
                wifiLock?.acquire()
            }
        } catch (e: Throwable) {
            AppLogger.warn("Service", "Could not acquire Wi-Fi/Power WakeLock: ${e.message}")
        }
    }

    private fun releasePowerAndWifiLocks() {
        try { if (wifiLock?.isHeld == true) wifiLock?.release() } catch (_: Exception) {}
        try { if (wakeLock?.isHeld == true) wakeLock?.release() } catch (_: Exception) {}
    }

    fun rotatePinAndDisconnectOthers(): String {
        val newPin = securityGate.rotatePin()
        server?.disconnectActiveClient()
        AppLogger.warn("Security", "PIN rotated -> Active client evicted and strict Wi-Fi PIN mode enabled.")
        return newPin
    }

    fun setConnectionArmed(armed: Boolean) {
        isConnectionArmed = armed
        securityGate.connectionEnabled = armed
        if (!armed) {
            AppLogger.warn("Security", "KILL-SWITCH ACTIVATED: Disconnecting active stream, closing ports 4748/4749, and locking Camera/Mic.")
            unregisterMdns()
            stopUdpBeacon()
            server?.stop()
            server = null
            stopHardwarePipelineInternal()
            streamRequested = false
            releasePowerAndWifiLocks()
            try {
                val nm = getSystemService(NotificationManager::class.java)
                nm?.notify(1001, buildNotification("Disconnected (Security Lock Active • Camera & Network Off)"))
            } catch (_: Exception) {}
            onUiStateChange?.invoke("disconnected", false, "Security Lock Active • Zero Open Ports")
        } else {
            AppLogger.info("Security", "Connection ARMED by user: Re-enabling ports 4748/4749 and Camera/Mic access.")
            securityGate.requirePinOnWifi = false
            securityGate.resetLockouts()
            acquirePowerAndWifiLocks()
            restartServer(usbOnly = usbOnlyMode)
            try {
                val nm = getSystemService(NotificationManager::class.java)
                nm?.notify(1001, buildNotification("Ready for OBS Studio & NVIDIA Broadcast connection (USB & Wi-Fi)"))
            } catch (_: Exception) {}
            onUiStateChange?.invoke("idle", false, "Standby")
        }
    }

    fun restartServer(usbOnly: Boolean) {
        usbOnlyMode = usbOnly
        unregisterMdns()
        server?.stop()
        server = null
        if (!isConnectionArmed) {
            stopUdpBeacon()
            return
        }

        val srv = CamPhoneServer(
            port = 4748,
            usbOnlyLoopback = usbOnly,
            securityGate = securityGate,
            capabilitiesJsonProvider = { CapabilityProbe.toJsonString(capabilities) },
            onStartStream = { lensId, w, h, fps, codec, hdr ->
                startHardwarePipeline(lensId, w, h, fps, codec, hdr)
            },
            onStopStream = {
                stopHardwarePipeline()
            },
            onControlPatch = { rawPatch ->
                val lens = capabilities.lenses.find { it.id == rawPatch.lensId } ?: capabilities.lenses.firstOrNull()
                if (lens != null) {
                    val bounds = CameraBounds(
                        minZoom = 1.0f,
                        maxZoom = lens.maxZoom,
                        minIso = lens.isoRange.first,
                        maxIso = lens.isoRange.last,
                        minExposureNs = lens.exposureNsRange.first,
                        maxExposureNs = lens.exposureNsRange.last,
                        minEv = -12,
                        maxEv = 12,
                        allowedLenses = capabilities.lenses.map { it.id }.toSet()
                    )
                    val clean = SecurityGate.sanitizeControlPatch(rawPatch, bounds)
                    cameraController?.applySanitizedControls(clean)
                }
            },
            onTallyUpdate = { tally ->
                onUiStateChange?.invoke(tally, videoEncoder != null, "")
            }
        )
        srv.start()
        server = srv

        if (!usbOnly) {
            registerMdns(4748)
            startUdpBeacon(4748)
        } else {
            stopUdpBeacon()
        }
    }

    private fun startHardwarePipeline(lensId: String, w: Int, h: Int, fps: Int, codec: String, hdr: Boolean) {
        lastStreamParams = StreamParams(lensId, w, h, fps, codec, hdr)
        streamRequested = true
        stopHardwarePipelineInternal()
        val cam = cameraController ?: return
        try {
            val (actualSize, actualFpsRange) = cam.pickSupportedResolutionAndFps(lensId, w, h, fps)
            val isWifi = (server?.lastStreamRemoteIp ?: "127.0.0.1") != "127.0.0.1"
            val bitrate = if (isWifi) {
                when {
                    actualSize.width >= 3840 -> 50_000_000
                    actualSize.width >= 2560 -> 30_000_000
                    else -> 20_000_000
                }
            } else {
                when {
                    actualSize.width >= 3840 -> 100_000_000
                    actualSize.width >= 2560 -> 50_000_000
                    else -> 35_000_000
                }
            }
            val vEnc = HardwareVideoEncoder(
                codec,
                actualSize.width,
                actualSize.height,
                actualFpsRange.upper,
                bitrate,
                hdr
            ) { hdrBytes, payload ->
                server?.sendMediaPacket(hdrBytes, payload)
            }
            val surface = vEnc.start()
            videoEncoder = vEnc
            cam.startSession(lensId, surface, actualFpsRange, hdr)

            val aEnc = HardwareAudioEncoder { hdrBytes, payload ->
                server?.sendMediaPacket(hdrBytes, payload)
            }
            aEnc.start()
            audioEncoder = aEnc

            val summary = "${actualSize.width}x${actualSize.height} @ ${actualFpsRange.upper}fps • ${codec.uppercase()}${if (hdr) " HDR10" else ""}"
            AppLogger.info("Pipeline", "Hardware pipeline ACTIVE: $summary")
            onUiStateChange?.invoke("program", true, summary)
        } catch (e: Throwable) {
            AppLogger.error("Pipeline", "Failed to start hardware pipeline", e)
            onUiStateChange?.invoke("idle", false, "Erro HW: ${e.message}")
        }
    }

    fun onPermissionsGranted() {
        val params = lastStreamParams
        if (streamRequested && params != null) {
            AppLogger.info("Service", "Permissions granted while stream requested — restarting camera session")
            startHardwarePipeline(params.lensId, params.w, params.h, params.fps, params.codec, params.hdr)
        }
    }

    private fun stopHardwarePipelineInternal() {
        audioEncoder?.stop()
        audioEncoder = null
        cameraController?.stopSession()
        videoEncoder?.stop()
        videoEncoder = null
    }

    private fun stopHardwarePipeline() {
        streamRequested = false
        stopHardwarePipelineInternal()
        if (isConnectionArmed) {
            onUiStateChange?.invoke("idle", false, "Standby")
        } else {
            onUiStateChange?.invoke("disconnected", false, "Security Lock Active • Zero Open Ports")
        }
    }

    private fun registerMdns(port: Int) {
        val mgr = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return
        val info = NsdServiceInfo().apply {
            serviceName = "CameraPhone-${Build.MODEL}"
            serviceType = "_cameraphone._tcp."
            setPort(port)
        }
        val listener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(serviceInfo: NsdServiceInfo?) {
                AppLogger.info("mDNS", "Registered _cameraphoneobs._tcp on port $port")
            }
            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {
                AppLogger.warn("mDNS", "Registration failed code=$errorCode")
            }
            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo?) {}
            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {}
        }
        try {
            mgr.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
            nsdManager = mgr
            nsdListener = listener
        } catch (e: Throwable) {
            AppLogger.warn("mDNS", "Could not register NSD service: ${e.message}")
        }
    }

    private fun unregisterMdns() {
        val mgr = nsdManager ?: return
        val l = nsdListener ?: return
        try { mgr.unregisterService(l) } catch (_: Exception) {}
        nsdListener = null
    }

    private fun startUdpBeacon(streamPort: Int) {
        stopUdpBeacon()
        udpRunning.set(true)
        Thread({
            var sock: DatagramSocket? = null
            try {
                sock = DatagramSocket(null).apply {
                    reuseAddress = true
                    broadcast = true
                    soTimeout = 1500
                    bind(InetSocketAddress(4749))
                }
                udpSocket = sock
                val recvBuf = ByteArray(256)
                while (udpRunning.get()) {
                    val beaconToken = if (securityGate.requirePinOnWifi) "PIN_REQUIRED" else "usb-zero-config"
                    val payload = "CAMPHONE_HERE_V1|$streamPort|$beaconToken|${Build.MANUFACTURER} ${Build.MODEL}"
                    val bytes = payload.toByteArray(Charsets.UTF_8)
                    try {
                        val bcast = DatagramPacket(bytes, bytes.size, InetAddress.getByName("255.255.255.255"), 4749)
                        sock.send(bcast)
                    } catch (_: Exception) {}

                    try {
                        val pkt = DatagramPacket(recvBuf, recvBuf.size)
                        sock.receive(pkt)
                        val msg = String(pkt.data, 0, pkt.length, Charsets.UTF_8)
                        if (msg.startsWith("CAMPHONE_DISCOVER_V1")) {
                            val reply = DatagramPacket(bytes, bytes.size, pkt.address, pkt.port)
                            sock.send(reply)
                        }
                    } catch (_: Exception) {}
                }
            } catch (e: Throwable) {
                AppLogger.warn("UDP-Beacon", "UDP discovery socket closed: ${e.message}")
            } finally {
                try { sock?.close() } catch (_: Exception) {}
            }
        }, "CamPhone-UDP-Beacon").apply { isDaemon = true; start() }
    }

    private fun stopUdpBeacon() {
        udpRunning.set(false)
        try { udpSocket?.close() } catch (_: Exception) {}
        udpSocket = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel("camphone_ch", "CameraPhone Service", NotificationManager.IMPORTANCE_LOW)
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(ch)
        }
    }

    private fun buildNotification(text: String): Notification {
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, "camphone_ch")
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setContentTitle("CameraPhone")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .build()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        stopHardwarePipeline()
        stopUdpBeacon()
        unregisterMdns()
        server?.stop()
        releasePowerAndWifiLocks()
        super.onDestroy()
    }
}
