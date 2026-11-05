package com.cameraphone.app.camera

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaRecorder
import com.cameraphone.app.protocol.CamPhonePacket
import com.cameraphone.app.util.AppLogger
import java.util.concurrent.atomic.AtomicBoolean

class HardwareAudioEncoder(
    private val sampleRate: Int = 48_000,
    private val bitrateBps: Int = 192_000,
    private val onPacket: (ByteArray, ByteArray) -> Unit
) {
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null

    @SuppressLint("MissingPermission")
    fun start() {
        if (running.getAndSet(true)) return
        worker = Thread({
            val channelConfig = AudioFormat.CHANNEL_IN_STEREO
            val minBuf = AudioRecord.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT)
                .coerceAtLeast(4096)

            val recorder = try {
                val r = AudioRecord(
                    MediaRecorder.AudioSource.CAMCORDER,
                    sampleRate,
                    channelConfig,
                    AudioFormat.ENCODING_PCM_16BIT,
                    minBuf * 2
                )
                if (r.state != AudioRecord.STATE_INITIALIZED) {
                    AppLogger.warn("AudioEncoder", "AudioRecord STATE_UNINITIALIZED (check RECORD_AUDIO permission). Continuing video-only.")
                    try { r.release() } catch (_: Throwable) {}
                    running.set(false)
                    return@Thread
                }
                r
            } catch (e: Throwable) {
                AppLogger.warn("AudioEncoder", "AudioRecord creation failed (${e.message}). Continuing video-only.")
                running.set(false)
                return@Thread
            }

            val codec = try {
                val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, 2).apply {
                    setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                    setInteger(MediaFormat.KEY_BIT_RATE, bitrateBps)
                    setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)
                }
                val mc = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
                mc.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                mc.start()
                recorder.startRecording()
                AppLogger.info("AudioEncoder", "Started HW AAC-LC audio encoder (${sampleRate}Hz stereo)")
                mc
            } catch (e: Throwable) {
                AppLogger.error("AudioEncoder", "Failed to start AAC audio encoder", e)
                try { recorder.release() } catch (_: Throwable) {}
                running.set(false)
                return@Thread
            }

            val pcmBuf = ByteArray(4096)
            val info = MediaCodec.BufferInfo()
            val startNs = System.nanoTime()

            try {
                while (running.get()) {
                    val readBytes = recorder.read(pcmBuf, 0, pcmBuf.size)
                    if (readBytes > 0) {
                        val inIdx = codec.dequeueInputBuffer(5_000L)
                        if (inIdx >= 0) {
                            val inBuf = codec.getInputBuffer(inIdx)
                            inBuf?.clear()
                            inBuf?.put(pcmBuf, 0, readBytes)
                            val ptsUs = (System.nanoTime() - startNs) / 1000L
                            codec.queueInputBuffer(inIdx, 0, readBytes, ptsUs, 0)
                        }
                    }

                    var outIdx = codec.dequeueOutputBuffer(info, 0L)
                    while (outIdx >= 0) {
                        val outBuf = codec.getOutputBuffer(outIdx)
                        if (outBuf != null && info.size > 0) {
                            outBuf.position(info.offset)
                            outBuf.limit(info.offset + info.size)
                            val payload = ByteArray(info.size)
                            outBuf.get(payload)
                            val flags = if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0)
                                CamPhonePacket.FLAG_CODEC_CONFIG else 0
                            val hdr = CamPhonePacket.encodeHeader(
                                streamType = CamPhonePacket.STREAM_AUDIO,
                                flags = flags,
                                ptsUs = info.presentationTimeUs,
                                payloadSize = payload.size
                            )
                            onPacket(hdr, payload)
                        }
                        codec.releaseOutputBuffer(outIdx, false)
                        outIdx = codec.dequeueOutputBuffer(info, 0L)
                    }
                }
            } catch (e: Throwable) {
                AppLogger.error("AudioEncoder", "Audio loop stopped", e)
            } finally {
                try { recorder.stop() } catch (_: Exception) {}
                try { recorder.release() } catch (_: Exception) {}
                try { codec.stop() } catch (_: Exception) {}
                try { codec.release() } catch (_: Exception) {}
            }
        }, "CamPhone-AudioEncoder").apply { isDaemon = true; start() }
    }

    fun stop() {
        running.set(false)
        worker?.join(500)
        worker = null
    }
}
