package com.cameraphone.app.probe

import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import kotlin.math.roundToInt

object CapabilityProbe {

    fun probeDevice(context: Context): DeviceCapabilityProfile {
        val cm = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val codecs = probeHardwareEncoders()
        val hdrSet = mutableSetOf<String>()
        val lenses = mutableListOf<LensDescriptor>()

        val seenIds = mutableSetOf<String>()
        for (cameraId in cm.cameraIdList) {
            processCameraId(cm, cameraId, seenIds, lenses, hdrSet)
            val chars = cm.getCameraCharacteristics(cameraId)
            for (physId in chars.physicalCameraIds) {
                processCameraId(cm, physId, seenIds, lenses, hdrSet)
            }
        }

        val socName = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else Build.HARDWARE
        return DeviceCapabilityProfile(
            deviceModel = "${Build.MANUFACTURER} ${Build.MODEL}",
            soc = socName,
            codecs = codecs,
            hdrCapabilities = hdrSet.sorted(),
            lenses = lenses
        )
    }

    private fun processCameraId(
        cm: CameraManager,
        id: String,
        seen: MutableSet<String>,
        outLenses: MutableList<LensDescriptor>,
        outHdr: MutableSet<String>
    ) {
        if (!seen.add(id)) return
        val chars = try {
            cm.getCameraCharacteristics(id)
        } catch (_: Exception) {
            return
        }

        val facingInt = chars.get(CameraCharacteristics.LENS_FACING) ?: return
        val facing = when (facingInt) {
            CameraCharacteristics.LENS_FACING_FRONT -> "front"
            CameraCharacteristics.LENS_FACING_BACK -> "back"
            else -> "external"
        }

        // Compute 35mm equivalent focal length
        val focalLengths = chars.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)
        val sensorSize = chars.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)
        val focal35mm = if (focalLengths != null && focalLengths.isNotEmpty() && sensorSize != null && sensorSize.width > 0f) {
            ((focalLengths[0] * 36.0f) / sensorSize.width).roundToInt()
        } else 24

        val label = when {
            facing == "front" -> "Front Camera (${focal35mm}mm)"
            focal35mm <= 18 -> "Ultra-Wide (${focal35mm}mm)"
            focal35mm in 19..35 -> "Main Wide (${focal35mm}mm)"
            else -> "Telephoto (${focal35mm}mm)"
        }

        val map = chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return
        val sizes = map.getOutputSizes(ImageFormat.PRIVATE) ?: emptyArray()
        val highSpeedSizes = map.highSpeedVideoSizes?.toSet() ?: emptySet()
        val fpsRanges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES) ?: emptyArray()
        val maxNormalFps = fpsRanges.maxOfOrNull { it.upper } ?: 30

        val resList = sizes
            .filter { it.width >= 1280 && it.height >= 720 && it.width * 9 == it.height * 16 }
            .sortedByDescending { it.width * it.height }
            .distinctBy { "${it.width}x${it.height}" }
            .map { sz ->
                val fpsOptions = mutableSetOf(24, 30)
                val minFrameDurationNs = map.getOutputMinFrameDuration(ImageFormat.PRIVATE, sz)
                if (minFrameDurationNs in 1..17_000_000L || maxNormalFps >= 60) {
                    fpsOptions.add(60)
                }
                if (highSpeedSizes.contains(sz)) {
                    val hsRanges = map.getHighSpeedVideoFpsRangesFor(sz)
                    if (hsRanges.any { it.upper >= 120 }) fpsOptions.add(120)
                }
                ResolutionCap(sz.width, sz.height, fpsOptions.sorted())
            }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val dynProfiles = chars.get(CameraCharacteristics.REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES)
            val supportedProfiles = dynProfiles?.supportedProfiles ?: emptySet()
            // HLG10 = 2, HDR10 = 4
            if (supportedProfiles.contains(2L)) outHdr.add("hlg10")
            if (supportedProfiles.contains(4L)) outHdr.add("hdr10")
        }

        val oisModes = chars.get(CameraCharacteristics.LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION)
        val hasOis = oisModes?.any { it == CameraCharacteristics.LENS_OPTICAL_STABILIZATION_MODE_ON } == true

        val isoRange = chars.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
        val expRange = chars.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
        val maxZoom = chars.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM) ?: 8.0f

        outLenses.add(
            LensDescriptor(
                id = id,
                facing = facing,
                focalLength35mmEq = focal35mm,
                label = label,
                resolutions = resList,
                oisAvailable = hasOis,
                isoRange = IntRange(isoRange?.lower ?: 50, isoRange?.upper ?: 6400),
                exposureNsRange = LongRange(expRange?.lower ?: 100_000L, expRange?.upper ?: 250_000_000L),
                maxZoom = maxZoom
            )
        )
    }

    private fun probeHardwareEncoders(): List<String> {
        val list = MediaCodecList(MediaCodecList.REGULAR_CODECS)
        val supported = mutableSetOf<String>()
        for (info in list.codecInfos) {
            if (!info.isEncoder) continue
            val isHw = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                info.isHardwareAccelerated
            } else {
                !info.name.startsWith("OMX.google.", ignoreCase = true) && !info.name.startsWith("c2.android.", ignoreCase = true)
            }
            if (!isHw) continue
            for (type in info.supportedTypes) {
                when (type.lowercase()) {
                    MediaFormat.MIMETYPE_VIDEO_HEVC -> supported.add("hevc")
                    MediaFormat.MIMETYPE_VIDEO_AVC -> supported.add("avc")
                    "video/av01" -> supported.add("av1")
                }
            }
        }
        return listOf("hevc", "av1", "avc").filter { supported.contains(it) }.ifEmpty { listOf("avc") }
    }

    private fun esc(s: String): String =
        s.replace("\\", "\\\\").replace("\"", "\\\"")

    fun toJsonString(profile: DeviceCapabilityProfile): String {
        val codecsStr = profile.codecs.joinToString(",") { "\"${esc(it)}\"" }
        val hdrStr = profile.hdrCapabilities.joinToString(",") { "\"${esc(it)}\"" }
        val lensesStr = profile.lenses.joinToString(",") { l ->
            val resStr = l.resolutions.joinToString(",") { r ->
                val fpsStr = r.fps.joinToString(",") { it.toString() }
                "{\"width\":${r.width},\"height\":${r.height},\"fps\":[$fpsStr]}"
            }
            "{\"id\":\"${esc(l.id)}\",\"facing\":\"${esc(l.facing)}\",\"focalLength35mmEq\":${l.focalLength35mmEq}," +
                "\"label\":\"${esc(l.label)}\",\"oisAvailable\":${l.oisAvailable}," +
                "\"isoMin\":${l.isoRange.first},\"isoMax\":${l.isoRange.last}," +
                "\"expMinNs\":${l.exposureNsRange.first},\"expMaxNs\":${l.exposureNsRange.last}," +
                "\"maxZoom\":${l.maxZoom},\"resolutions\":[$resStr]}"
        }
        return "{\"deviceModel\":\"${esc(profile.deviceModel)}\",\"soc\":\"${esc(profile.soc)}\"," +
            "\"codecs\":[$codecsStr],\"hdrCapabilities\":[$hdrStr],\"lenses\":[$lensesStr]}"
    }
}
