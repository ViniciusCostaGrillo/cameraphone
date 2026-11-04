package com.cameraphone.obs.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

data class DecodedHeader(
    val streamType: Int,
    val flags: Int,
    val ptsUs: Long,
    val payloadSize: Int
)

object CamPhonePacket {
    const val MAGIC: Int = 0x4350
    const val VERSION: Int = 0x01
    const val HEADER_SIZE: Int = 16
    const val MAX_PAYLOAD_SIZE: Int = 8 * 1024 * 1024

    const val STREAM_VIDEO: Int = 0x01
    const val STREAM_AUDIO: Int = 0x02
    const val STREAM_METADATA: Int = 0x03
    const val STREAM_KEEPALIVE: Int = 0x04

    const val FLAG_KEYFRAME: Int = 1 shl 0
    const val FLAG_CODEC_CONFIG: Int = 1 shl 1
    const val FLAG_HDR_10BIT: Int = 1 shl 2
    const val FLAG_DISCONT: Int = 1 shl 3

    fun encodeHeader(streamType: Int, flags: Int, ptsUs: Long, payloadSize: Int): ByteArray {
        require(payloadSize in 0..MAX_PAYLOAD_SIZE) { "Payload exceeds 8 MiB security limit" }
        val buf = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.BIG_ENDIAN)
        buf.putShort(MAGIC.toShort())
        buf.put(VERSION.toByte())
        buf.put(streamType.toByte())
        buf.putLong(ptsUs)
        val packedSizeAndFlags = ((flags and 0xFF) shl 24) or (payloadSize and 0x00FFFFFF)
        buf.putInt(packedSizeAndFlags)
        return buf.array()
    }

    fun decodeHeader(bytes: ByteArray): DecodedHeader {
        require(bytes.size == HEADER_SIZE) { "Header must be 16 bytes" }
        val buf = ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN)
        val magic = buf.short.toInt() and 0xFFFF
        require(magic == MAGIC) { "Invalid magic bytes" }
        val ver = buf.get().toInt() and 0xFF
        require(ver == VERSION) { "Unsupported version: $ver" }
        val streamType = buf.get().toInt() and 0xFF
        val ptsUs = buf.long
        val packed = buf.int
        val flags = (packed ushr 24) and 0xFF
        val payloadSize = packed and 0x00FFFFFF
        require(payloadSize <= MAX_PAYLOAD_SIZE) { "Payload size exceeds 8 MiB" }
        return DecodedHeader(streamType, flags, ptsUs, payloadSize)
    }
}
