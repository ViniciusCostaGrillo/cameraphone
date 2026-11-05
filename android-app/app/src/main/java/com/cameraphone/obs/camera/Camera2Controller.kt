package com.cameraphone.obs.camera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.util.Range
import android.util.Size
import android.view.Surface
import com.cameraphone.obs.security.RawControlPatch
import com.cameraphone.obs.util.AppLogger
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

    fun pickSupportedResolutionAndFps(
        lensId: String,
        requestedWidth: Int,
        requestedHeight: Int,
        requestedFps: Int
    ): Pair<Size, Range<Int>> {
        try {
            val chars = cameraManager.getCameraCharacteristics(lensId)
            val map = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val sizes = map?.getOutputSizes(SurfaceTexture::class.java)?.toList().orEmpty()
            val chosenSize = if (sizes.isNotEmpty()) {
                sizes.firstOrNull { it.width == requestedWidth && it.height == requestedHeight }
                    ?: sizes.filter { it.width <= requestedWidth && it.height <= requestedHeight }
                        .maxByOrNull { it.width * it.height }
                    ?: sizes.firstOrNull { it.width == 1920 && it.height == 1080 }
                    ?: sizes.first()
            } else {
                Size(1920, 1080)
            }

            val fpsRanges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)?.toList().orEmpty()
            val chosenFps = if (fpsRanges.isNotEmpty()) {
                fpsRanges.firstOrNull { it.lower == requestedFps && it.upper == requestedFps }
                    ?: fpsRanges.firstOrNull { it.upper == requestedFps }
                    ?: fpsRanges.filter { it.upper <= requestedFps }.maxByOrNull { it.upper }
                    ?: fpsRanges.maxByOrNull { it.upper }!!
            } else {
                Range(30, 30)
            }

            if (chosenSize.width != requestedWidth || chosenSize.height != requestedHeight || chosenFps.upper != requestedFps) {
                AppLogger.warn(
                    "Camera2",
                    "Requested ${requestedWidth}x${requestedHeight}@${requestedFps}fps on lens $lensId -> adjusted to ${chosenSize.width}x${chosenSize.height}@${chosenFps.lower}-${chosenFps.upper}fps"
                )
            } else {
                AppLogger.info(
                    "Camera2",
                    "Verified hardware support for ${chosenSize.width}x${chosenSize.height}@${chosenFps.upper}fps on lens $lensId"
                )
            }
            return chosenSize to chosenFps
        } catch (e: Throwable) {
            AppLogger.error("Camera2", "Failed to query lens $lensId characteristics, falling back to 1080p30", e)
            return Size(1920, 1080) to Range(30, 30)
        }
    }

    @SuppressLint("MissingPermission")
    fun startSession(
        lensId: String,
        encoderSurface: Surface,
        fpsRange: Range<Int>,
        hdr10Bit: Boolean
    ) {
        stopSession()
        activeSurface = encoderSurface
        activeFps = fpsRange.upper

        val thread = HandlerThread("CamPhone-Camera2").apply { start() }
        val handler = Handler(thread.looper)
        cameraThread = thread
        cameraHandler = handler

        try {
            val availableIds = cameraManager.cameraIdList.toList()
            val resolvedLensId = if (availableIds.contains(lensId)) lensId else (availableIds.firstOrNull() ?: "0")
            AppLogger.info("Camera2", "Opening camera lensId=$resolvedLensId (available=$availableIds)...")
            cameraManager.openCamera(resolvedLensId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    AppLogger.info("Camera2", "CameraDevice opened (id=${camera.id}). Configuring capture session...")
                    cameraDevice = camera
                    createCapturePipeline(camera, encoderSurface, fpsRange, hdr10Bit, handler)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    AppLogger.warn("Camera2", "CameraDevice disconnected (id=${camera.id})")
                    camera.close()
                    cameraDevice = null
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    AppLogger.error("Camera2", "CameraDevice error code=$error on id=${camera.id}")
                    camera.close()
                    cameraDevice = null
                }
            }, handler)
        } catch (e: SecurityException) {
            AppLogger.error("Camera2", "CAMERA permission denied! Grant Camera permission in Android Settings.", e)
        } catch (e: Throwable) {
            AppLogger.error("Camera2", "Exception while opening camera lens $lensId", e)
        }
    }

    private fun createCapturePipeline(
        camera: CameraDevice,
        surface: Surface,
        fpsRange: Range<Int>,
        hdr10Bit: Boolean,
        handler: Handler
    ) {
        try {
            val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                addTarget(surface)
                set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fpsRange)
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                set(CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE, CaptureRequest.LENS_OPTICAL_STABILIZATION_MODE_ON)
            }
            requestBuilder = builder

            val outConfig = OutputConfiguration(surface)
            if (hdr10Bit && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                try {
                    outConfig.dynamicRangeProfile = 2L // DynamicRangeProfiles.HLG10
                } catch (e: Throwable) {
                    AppLogger.warn("Camera2", "HLG10 dynamicRangeProfile not supported on this lens, using SDR")
                }
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val sessionConfig = SessionConfiguration(
                    SessionConfiguration.SESSION_REGULAR,
                    listOf(outConfig),
                    Executors.newSingleThreadExecutor(),
                    object : CameraCaptureSession.StateCallback() {
                        override fun onConfigured(session: CameraCaptureSession) {
                            captureSession = session
                            try {
                                session.setRepeatingRequest(builder.build(), null, handler)
                                AppLogger.info("Camera2", "CameraCaptureSession repeating request ACTIVE!")
                            } catch (e: Throwable) {
                                AppLogger.error("Camera2", "setRepeatingRequest failed", e)
                            }
                        }
                        override fun onConfigureFailed(session: CameraCaptureSession) {
                            AppLogger.error("Camera2", "CameraCaptureSession onConfigureFailed! Surface resolution/format rejected by sensor.")
                        }
                    }
                )
                camera.createCaptureSession(sessionConfig)
            }
        } catch (e: Throwable) {
            AppLogger.error("Camera2", "Failed to create capture pipeline", e)
        }
    }

    fun applySanitizedControls(patch: RawControlPatch) {
        val builder = requestBuilder ?: return
        val session = captureSession ?: return
        val handler = cameraHandler ?: return

        try {
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
            session.setRepeatingRequest(builder.build(), null, handler)
            AppLogger.info("Camera2", "Applied live control patch: zoom=${patch.zoomRatio}, iso=${patch.iso}, torch=${patch.torch}")
        } catch (e: Throwable) {
            AppLogger.error("Camera2", "Failed to apply live control patch", e)
        }
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
