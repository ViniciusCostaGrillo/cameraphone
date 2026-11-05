package com.cameraphone.app.camera

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Bundle
import android.view.Surface
import com.cameraphone.app.protocol.CamPhonePacket
import com.cameraphone.app.util.AppLogger
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class HardwareVideoEncoder(
    private val codecName: String,
    private val width: Int,
    private val height: Int,
    private val fps: Int,
    private val bitrateBps: Int,
    private val hdr10Bit: Boolean,
    private val onPacket: (ByteArray, ByteArray) -> Unit
) {
    private var codec: MediaCodec? = null
    var inputSurface: Surface? = null
        private set
    var actualWidth: Int = width
        private set
    var actualHeight: Int = height
        private set
    var actualMime: String = resolveMimeType(codecName)
        private set
    private val running = AtomicBoolean(false)
    private var drainThread: Thread? = null
    private var packetsSent: Long = 0
    @Volatile
    private var cachedCodecConfig: ByteArray? = null

    fun start(): Surface {
        var mime = resolveMimeType(codecName)
        var mc: MediaCodec? = null
        var surface: Surface? = null

        val attempts = listOf(
            Triple(mime, width, height),
            Triple(mime, 1920, 1080),
            Triple(MediaFormat.MIMETYPE_VIDEO_HEVC, 1920, 1080),
            Triple(MediaFormat.MIMETYPE_VIDEO_AVC, 1920, 1080),
            Triple(MediaFormat.MIMETYPE_VIDEO_AVC, 1280, 720)
        )

        var lastErr: Throwable? = null
        for ((tryMime, tryW, tryH) in attempts) {
            try {
                val candidate = MediaCodec.createEncoderByType(tryMime)
                val tryFps = if (tryW < width) 30 else fps
                var tryBitrate = if (tryW >= 3840) clampBitrateBps(bitrateBps) else 20_000_000
                try {
                    val vCaps = candidate.codecInfo.getCapabilitiesForType(tryMime)?.videoCapabilities
                    if (vCaps != null) {
                        tryBitrate = vCaps.bitrateRange.clamp(tryBitrate)
                    }
                } catch (_: Throwable) {}

                val format = MediaFormat.createVideoFormat(tryMime, tryW, tryH).apply {
                    setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                    setInteger(MediaFormat.KEY_BIT_RATE, tryBitrate)
                    setInteger(MediaFormat.KEY_FRAME_RATE, tryFps)
                    setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        setInteger(MediaFormat.KEY_PREPEND_HEADER_TO_SYNC_FRAMES, 1)
                    }
                    if (hdr10Bit && tryMime == MediaFormat.MIMETYPE_VIDEO_HEVC && tryW == width) {
                        setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                        setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
                        setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_HLG)
                    }
                }
                candidate.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                val surf = candidate.createInputSurface()
                candidate.start()
                mc = candidate
                surface = surf
                mime = tryMime
                actualWidth = tryW
                actualHeight = tryH
                actualMime = tryMime
                AppLogger.info("VideoEncoder", "Started HW encoder: mime=$tryMime, ${tryW}x${tryH}@${tryFps}fps, bitrate=${tryBitrate / 1_000_000}Mbps")
                break
            } catch (e: Throwable) {
                lastErr = e
                AppLogger.warn("VideoEncoder", "Encoder config failed for $tryMime ${tryW}x${tryH}: ${e.message}")
            }
        }

        if (mc == null || surface == null) {
            AppLogger.error("VideoEncoder", "All HW encoder configurations failed!", lastErr)
            throw IllegalStateException("Unable to configure MediaCodec video encoder", lastErr)
        }

        codec = mc
        inputSurface = surface
        running.set(true)
        drainThread = Thread(::drainLoop, "CamPhone-VideoEncoder").apply { isDaemon = true; start() }
        return surface
    }

    fun requestImmediateKeyframe() {
        val params = Bundle().apply {
            putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
        }
        try {
            codec?.setParameters(params)
        } catch (_: Exception) {}
    }

    private fun drainLoop() {
        val mc = codec ?: return
        val info = MediaCodec.BufferInfo()
        while (running.get()) {
            val outIndex = try {
                mc.dequeueOutputBuffer(info, 10_000L)
            } catch (e: Exception) {
                if (running.get()) {
                    AppLogger.error("VideoEncoder", "dequeueOutputBuffer exception", e)
                }
                break
            }
            if (outIndex >= 0) {
                val buf: ByteBuffer? = mc.getOutputBuffer(outIndex)
                if (buf != null && info.size > 0) {
                    buf.position(info.offset)
                    buf.limit(info.offset + info.size)
                    val rawPayload = ByteArray(info.size)
                    buf.get(rawPayload)

                    var flags = 0
                    val isConfig = (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0
                    val isKey = (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                    if (isConfig) {
                        flags = flags or CamPhonePacket.FLAG_CODEC_CONFIG
                        cachedCodecConfig = rawPayload
                    }
                    if (isKey) {
                        flags = flags or CamPhonePacket.FLAG_KEYFRAME
                    }
                    if (hdr10Bit) {
                        flags = flags or CamPhonePacket.FLAG_HDR_10BIT
                    }

                    val cfg = cachedCodecConfig
                    val payload = if (isKey && !isConfig && cfg != null && !startsWithBytes(rawPayload, cfg)) {
                        ByteArray(cfg.size + rawPayload.size).also { combined ->
                            System.arraycopy(cfg, 0, combined, 0, cfg.size)
                            System.arraycopy(rawPayload, 0, combined, cfg.size, rawPayload.size)
                        }
                    } else {
                        rawPayload
                    }

                    val hdr = CamPhonePacket.encodeHeader(
                        streamType = CamPhonePacket.STREAM_VIDEO,
                        flags = flags,
                        ptsUs = info.presentationTimeUs,
                        payloadSize = payload.size
                    )
                    packetsSent++
                    if (packetsSent == 1L || packetsSent % 120L == 0L) {
                        AppLogger.info("VideoEncoder", "Encoded packet #$packetsSent (${payload.size} bytes, flags=$flags)")
                    }
                    onPacket(hdr, payload)
                }
                try {
                    mc.releaseOutputBuffer(outIndex, false)
                } catch (_: Exception) {}
            }
        }
    }

    fun stop() {
        running.set(false)
        drainThread?.join(500)
        try { codec?.stop() } catch (_: Exception) {}
        try { codec?.release() } catch (_: Exception) {}
        try { inputSurface?.release() } catch (_: Exception) {}
        codec = null
        inputSurface = null
    }

    companion object {
        fun resolveMimeType(codec: String): String = when (codec.lowercase()) {
            "hevc", "h265" -> MediaFormat.MIMETYPE_VIDEO_HEVC
            "av1", "av01"  -> "video/av01"
            else           -> MediaFormat.MIMETYPE_VIDEO_AVC
        }

        fun clampBitrateBps(bps: Int): Int = bps.coerceIn(2_000_000, 150_000_000)

        private fun startsWithBytes(data: ByteArray, prefix: ByteArray): Boolean {
            if (data.size < prefix.size || prefix.isEmpty()) return false
            for (i in prefix.indices) {
                if (data[i] != prefix[i]) return false
            }
            return true
        }
    }
}
