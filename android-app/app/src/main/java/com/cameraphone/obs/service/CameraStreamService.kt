package com.cameraphone.obs.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import com.cameraphone.obs.camera.Camera2Controller
import com.cameraphone.obs.camera.HardwareAudioEncoder
import com.cameraphone.obs.camera.HardwareVideoEncoder
import com.cameraphone.obs.net.CamPhoneServer
import com.cameraphone.obs.probe.CapabilityProbe
import com.cameraphone.obs.probe.DeviceCapabilityProfile
import com.cameraphone.obs.security.CameraBounds
import com.cameraphone.obs.security.SecurityGate
import com.cameraphone.obs.util.AppLogger

class CameraStreamService : Service() {

    inner class LocalBinder : Binder() {
        fun getService(): CameraStreamService = this@CameraStreamService
    }

    private val binder = LocalBinder()
    val securityGate = SecurityGate()

    private var server: CamPhoneServer? = null
    private var cameraController: Camera2Controller? = null
    private var videoEncoder: HardwareVideoEncoder? = null
    private var audioEncoder: HardwareAudioEncoder? = null
    private var nsdManager: NsdManager? = null
    private var nsdListener: NsdManager.RegistrationListener? = null
    private lateinit var capabilities: DeviceCapabilityProfile

    @Volatile
    var usbOnlyMode: Boolean = false
        private set

    var onUiStateChange: ((tally: String, streaming: Boolean, activeInfo: String) -> Unit)? = null

    override fun onCreate() {
        super.onCreate()
        AppLogger.info("Service", "CameraStreamService starting on ${Build.MANUFACTURER} ${Build.MODEL} (SDK ${Build.VERSION.SDK_INT})")
        createNotificationChannel()
        startForeground(1001, buildNotification("Ready for OBS Studio connection (USB & Wi-Fi)"))
        capabilities = CapabilityProbe.probeDevice(this)
        AppLogger.info("Service", "Probed ${capabilities.lenses.size} lenses, codecs=${capabilities.codecs}")
        cameraController = Camera2Controller(this)
        // Default to usbOnly = false (0.0.0.0:4748) so BOTH USB Cable (127.0.0.1) and Wi-Fi IP work out-of-the-box!
        restartServer(usbOnly = false)
    }

    fun restartServer(usbOnly: Boolean) {
        usbOnlyMode = usbOnly
        unregisterMdns()
        server?.stop()

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
        }
    }

    private fun startHardwarePipeline(lensId: String, w: Int, h: Int, fps: Int, codec: String, hdr: Boolean) {
        stopHardwarePipeline()
        val cam = cameraController ?: return
        try {
            val (actualSize, actualFpsRange) = cam.pickSupportedResolutionAndFps(lensId, w, h, fps)
            val bitrate = if (actualSize.width >= 3840) 80_000_000 else 30_000_000
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

    private fun stopHardwarePipeline() {
        audioEncoder?.stop()
        audioEncoder = null
        cameraController?.stopSession()
        videoEncoder?.stop()
        videoEncoder = null
        onUiStateChange?.invoke("idle", false, "Standby")
    }

    private fun registerMdns(port: Int) {
        val mgr = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return
        val info = NsdServiceInfo().apply {
            serviceName = "CameraPhone-${Build.MODEL}"
            serviceType = "_cameraphoneobs._tcp."
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

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel("camphone_ch", "CameraPhone OBS Service", NotificationManager.IMPORTANCE_LOW)
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
            .setContentTitle("CameraPhone OBS")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .build()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        stopHardwarePipeline()
        unregisterMdns()
        server?.stop()
        super.onDestroy()
    }
}
