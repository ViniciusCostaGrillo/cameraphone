package com.cameraphone.obs.probe

data class ResolutionCap(
    val width: Int,
    val height: Int,
    val fps: List<Int>
)

data class LensDescriptor(
    val id: String,
    val facing: String,
    val focalLength35mmEq: Int,
    val label: String,
    val resolutions: List<ResolutionCap>,
    val oisAvailable: Boolean,
    val isoRange: IntRange,
    val exposureNsRange: LongRange,
    val maxZoom: Float
)

data class DeviceCapabilityProfile(
    val deviceModel: String,
    val soc: String,
    val codecs: List<String>,
    val hdrCapabilities: List<String>,
    val lenses: List<LensDescriptor>
)
