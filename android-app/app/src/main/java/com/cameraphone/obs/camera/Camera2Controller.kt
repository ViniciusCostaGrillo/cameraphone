package com.cameraphone.obs.camera

import android.annotation.SuppressLint
import android.content.Context
import android.hardware.camera2.*
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.util.Range
import android.view.Surface
import com.cameraphone.obs.security.RawControlPatch
import java.util.concurrent.Executors

class Camera2Controller(private val context: Context) {
    private val cameraManager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var requestBuilder: CaptureRequest.Builder? = null
    private var activeSurface: Surface? = null
    private var activeFps: Int = 60

    @SuppressLint("MissingPermission")
    fun startSession(
        lensId: String,
        encoderSurface: Surface,
        fps: Int,
        hdr10Bit: Boolean
    ) {
        stopSession()
        activeSurface = encoderSurface
        activeFps = fps

        val thread = HandlerThread("CamPhone-Camera2").apply { start() }
        val handler = Handler(thread.looper)
        cameraThread = thread
        cameraHandler = handler

        cameraManager.openCamera(lensId, object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                cameraDevice = camera
                createCapturePipeline(camera, encoderSurface, fps, hdr10Bit, handler)
            }

            override fun onDisconnected(camera: CameraDevice) {
                camera.close()
                cameraDevice = null
            }

            override fun onError(camera: CameraDevice, error: Int) {
                camera.close()
                cameraDevice = null
            }
        }, handler)
    }

    private fun createCapturePipeline(
        camera: CameraDevice,
        surface: Surface,
        fps: Int,
        hdr10Bit: Boolean,
        handler: Handler
    ) {
        val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
            addTarget(surface)
            set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(fps, fps))
            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
            set(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON)
        }
        requestBuilder = builder

        val outConfig = OutputConfiguration(surface)
        if (hdr10Bit && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // DynamicRangeProfiles.HLG10 = 2L
            outConfig.dynamicRangeProfile = 2L
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val sessionConfig = SessionConfiguration(
                SessionConfiguration.SESSION_REGULAR,
                listOf(outConfig),
                Executors.newSingleThreadExecutor(),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(session: CameraCaptureSession) {
                        captureSession = session
                        session.setRepeatingRequest(builder.build(), null, handler)
                    }
                    override fun onConfigureFailed(session: CameraCaptureSession) {}
                }
            )
            camera.createCaptureSession(sessionConfig)
        }
    }

    fun applySanitizedControls(patch: RawControlPatch) {
        val builder = requestBuilder ?: return
        val session = captureSession ?: return
        val handler = cameraHandler ?: return

        if (patch.zoomRatio != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            builder.set(CaptureRequest.CONTROL_ZOOM_RATIO, patch.zoomRatio)
        }
        if (patch.iso != null || patch.exposureTimeNs != null) {
            builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
            patch.iso?.let { builder.set(CaptureRequest.SENSOR_SENSITIVITY, it) }
            patch.exposureTimeNs?.let { builder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, it) }
        }
        if (patch.evStep != null) {
            builder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, patch.evStep)
        }
        if (patch.focusDistanceDiopters != null) {
            if (patch.focusDistanceDiopters <= 0.0f) {
                builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
            } else {
                builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF)
                builder.set(CaptureRequest.LENS_FOCUS_DISTANCE, patch.focusDistanceDiopters)
            }
        }
        if (patch.torch != null) {
            builder.set(
                CaptureRequest.FLASH_MODE,
                if (patch.torch) CaptureRequest.FLASH_MODE_TORCH else CaptureRequest.FLASH_MODE_OFF
            )
        }
        try {
            session.setRepeatingRequest(builder.build(), null, handler)
        } catch (_: Exception) {}
    }

    fun stopSession() {
        try { captureSession?.close() } catch (_: Exception) {}
        try { cameraDevice?.close() } catch (_: Exception) {}
        cameraThread?.quitSafely()
        captureSession = null
        cameraDevice = null
        cameraThread = null
        cameraHandler = null
    }
}
