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
    private val running = AtomicBoolean(false)
    private var drainThread: Thread? = null
    private var packetsSent: Long = 0

    fun start(): Surface {
        var mime = resolveMimeType(codecName)
        var mc: MediaCodec? = null
        var surface: Surface? = null

        val attempts = listOf(
            Triple(mime, width, height),
            Triple(MediaFormat.MIMETYPE_VIDEO_HEVC, 1920, 1080),
            Triple(MediaFormat.MIMETYPE_VIDEO_AVC, 1920, 1080)
        )

        var lastErr: Throwable? = null
        for ((tryMime, tryW, tryH) in attempts) {
            try {
                val tryFps = if (tryW < width) 30 else fps
                val tryBitrate = if (tryW >= 3840) clampBitrateBps(bitrateBps) else 20_000_000
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
                val candidate = MediaCodec.createEncoderByType(tryMime)
                candidate.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                val surf = candidate.createInputSurface()
                candidate.start()
                mc = candidate
                surface = surf
                mime = tryMime
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
                    val payload = ByteArray(info.size)
                    buf.get(payload)

                    var flags = 0
                    if ((info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                        flags = flags or CamPhonePacket.FLAG_KEYFRAME
                    }
                    if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                        flags = flags or CamPhonePacket.FLAG_CODEC_CONFIG
                    }
                    if (hdr10Bit) {
                        flags = flags or CamPhonePacket.FLAG_HDR_10BIT
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
    }
}
