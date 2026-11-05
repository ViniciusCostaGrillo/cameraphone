package com.cameraphone.obs.camera

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Bundle
import android.view.Surface
import com.cameraphone.obs.protocol.CamPhonePacket
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

    fun start(): Surface {
        val mime = resolveMimeType(codecName)
        val format = MediaFormat.createVideoFormat(mime, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, clampBitrateBps(bitrateBps))
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1) // 1s GOP for fast OBS recovery
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                setInteger(MediaFormat.KEY_PREPEND_HEADER_TO_SYNC_FRAMES, 1)
            }
            if (hdr10Bit && mime == MediaFormat.MIMETYPE_VIDEO_HEVC) {
                setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
                setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
                setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_HLG)
            }
        }

        val mc = MediaCodec.createEncoderByType(mime)
        mc.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        val surface = mc.createInputSurface()
        mc.start()

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
            } catch (_: Exception) {
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
