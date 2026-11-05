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
    var usbOnlyMode: Boolean = true
        private set

    var onUiStateChange: ((tally: String, streaming: Boolean, activeInfo: String) -> Unit)? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(1001, buildNotification("Ready for OBS Studio connection"))
        capabilities = CapabilityProbe.probeDevice(this)
        cameraController = Camera2Controller(this)
        restartServer(usbOnly = true)
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
        val bitrate = if (w >= 3840) 80_000_000 else 35_000_000
        val vEnc = HardwareVideoEncoder(codec, w, h, fps, bitrate, hdr) { hdrBytes, payload ->
            server?.sendMediaPacket(hdrBytes, payload)
        }
        val surface = vEnc.start()
        videoEncoder = vEnc
        cameraController?.startSession(lensId, surface, fps, hdr)

        val aEnc = HardwareAudioEncoder { hdrBytes, payload ->
            server?.sendMediaPacket(hdrBytes, payload)
        }
        aEnc.start()
        audioEncoder = aEnc

        val summary = "${w}x${h} @ ${fps}fps • ${codec.uppercase()}${if (hdr) " HDR10" else ""}"
        onUiStateChange?.invoke("program", true, summary)
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
            override fun onServiceRegistered(serviceInfo: NsdServiceInfo?) {}
            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {}
            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo?) {}
            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) {}
        }
        mgr.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
        nsdManager = mgr
        nsdListener = listener
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
