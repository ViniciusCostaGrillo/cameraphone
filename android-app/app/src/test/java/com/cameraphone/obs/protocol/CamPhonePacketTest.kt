package com.cameraphone.obs.protocol

import com.cameraphone.obs.camera.HardwareVideoEncoder
import com.cameraphone.obs.probe.*
import org.junit.Assert.*
import org.junit.Test

class CamPhonePacketTest {

    @Test
    fun `encodes 16-byte Big-Endian header matching C++ wire layout`() {
        val hdr = CamPhonePacket.encodeHeader(
            streamType = CamPhonePacket.STREAM_VIDEO,
            flags = CamPhonePacket.FLAG_KEYFRAME or CamPhonePacket.FLAG_HDR_10BIT,
            ptsUs = 0x0102030405060708L,
            payloadSize = 512000
        )
        assertEquals(16, hdr.size)
        assertEquals(0x43.toByte(), hdr[0])
        assertEquals(0x50.toByte(), hdr[1])
        assertEquals(0x01.toByte(), hdr[2])
        assertEquals(0x01.toByte(), hdr[3])
        assertEquals(0x01.toByte(), hdr[4])
        assertEquals(0x08.toByte(), hdr[11])
        assertEquals((CamPhonePacket.FLAG_KEYFRAME or CamPhonePacket.FLAG_HDR_10BIT).toByte(), hdr[12])

        val decoded = CamPhonePacket.decodeHeader(hdr)
        assertEquals(CamPhonePacket.STREAM_VIDEO, decoded.streamType)
        assertEquals(0x0102030405060708L, decoded.ptsUs)
        assertEquals(512000, decoded.payloadSize)
        assertEquals(CamPhonePacket.FLAG_KEYFRAME or CamPhonePacket.FLAG_HDR_10BIT, decoded.flags)
    }

    @Test
    fun `serializes device capabilities with lenses and 4k60 hdr support to json`() {
        val profile = DeviceCapabilityProfile(
            deviceModel = "Pixel 9 Pro",
            soc = "Tensor G4",
            codecs = listOf("hevc", "av1", "avc"),
            hdrCapabilities = listOf("hdr10", "hlg10"),
            lenses = listOf(
                LensDescriptor(
                    id = "0",
                    facing = "back",
                    focalLength35mmEq = 24,
                    label = "Main Wide (24mm)",
                    resolutions = listOf(
                        ResolutionCap(3840, 2160, listOf(30, 60)),
                        ResolutionCap(1920, 1080, listOf(30, 60, 120))
                    ),
                    oisAvailable = true,
                    isoRange = IntRange(50, 6400),
                    exposureNsRange = LongRange(100_000L, 250_000_000L),
                    maxZoom = 30.0f
                )
            )
        )
        val jsonStr = CapabilityProbe.toJsonString(profile)
        assertTrue(jsonStr.contains("\"deviceModel\":\"Pixel 9 Pro\""))
        assertTrue(jsonStr.contains("\"hevc\""))
        assertTrue(jsonStr.contains("\"hlg10\""))
        assertTrue(jsonStr.contains("\"width\":3840"))
        assertTrue(jsonStr.contains("120"))
    }

    @Test
    fun `maps codec names to MediaFormat mime types and clamps bitrate`() {
        assertEquals("video/hevc", HardwareVideoEncoder.resolveMimeType("hevc"))
        assertEquals("video/av01", HardwareVideoEncoder.resolveMimeType("av1"))
        assertEquals("video/avc", HardwareVideoEncoder.resolveMimeType("avc"))
        assertEquals(150_000_000, HardwareVideoEncoder.clampBitrateBps(500_000_000))
        assertEquals(2_000_000, HardwareVideoEncoder.clampBitrateBps(100_000))
    }
}
