package com.cameraphone.emulator

import com.cameraphone.app.net.CamPhoneServer
import com.cameraphone.app.protocol.CamPhonePacket
import com.cameraphone.app.security.SecurityGate
import com.cameraphone.app.util.AppLogger
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

object LiveAndroidEmulatorServer {
    @JvmStatic
    fun main(args: Array<String>) {
        val framesFile = args.getOrNull(0) ?: ""
        val durationSec = args.getOrNull(1)?.toIntOrNull() ?: 12
        val fixedPin = args.getOrNull(2)

        // Load pre-encoded Annex-B H.264 / HEVC frames from file (4-byte big-endian length + 1-byte flags + bytes)
        val frames = mutableListOf<Pair<Int, ByteArray>>()
        if (framesFile.isNotEmpty() && File(framesFile).exists()) {
            val raw = File(framesFile).readBytes()
            var offset = 0
            while (offset + 5 <= raw.size) {
                val len = ((raw[offset].toInt() and 0xFF) shl 24) or
                          ((raw[offset + 1].toInt() and 0xFF) shl 16) or
                          ((raw[offset + 2].toInt() and 0xFF) shl 8) or
                          (raw[offset + 3].toInt() and 0xFF)
                val flags = raw[offset + 4].toInt() and 0xFF
                offset += 5
                if (offset + len <= raw.size) {
                    frames.add(flags to raw.copyOfRange(offset, offset + len))
                }
                offset += len
            }
        }

        val gate = SecurityGate()
        val streaming = AtomicBoolean(false)
        var serverRef: CamPhoneServer? = null

        val capsJson = """{"deviceModel":"Android Emulator (CameraPhone 4K Pro)","codecs":["hevc","avc","av1"],"lenses":[{"id":"0","facing":"back","focalLengthsMm":[4.7],"maxZoom":10.0,"supportsHdr10":true,"modes":[{"w":3840,"h":2160,"fps":[30,60]},{"w":1920,"h":1080,"fps":[30,60,120]}]}]}"""

        val server = CamPhoneServer(
            port = 4748,
            usbOnlyLoopback = false,
            securityGate = gate,
            capabilitiesJsonProvider = { capsJson },
            onStartStream = { lensId, w, h, fps, codec, hdr ->
                AppLogger.info("Emulator", "OBS connected! Streaming ${w}x${h}@${fps}fps ($codec, hdr=$hdr) from lens $lensId")
                println("EMULATOR_STREAM_STARTED lens=$lensId res=${w}x${h} fps=$fps codec=$codec")
                System.out.flush()
                if (streaming.compareAndSet(false, true)) {
                    Thread({
                        var idx = 0L
                        val startUs = System.nanoTime() / 1000L
                        while (streaming.get()) {
                            if (frames.isNotEmpty()) {
                                val (flags, payload) = frames[(idx % frames.size).toInt()]
                                val ptsUs = (System.nanoTime() / 1000L) - startUs
                                val hdr16 = CamPhonePacket.encodeHeader(
                                    streamType = CamPhonePacket.STREAM_VIDEO,
                                    flags = flags,
                                    ptsUs = ptsUs,
                                    payloadSize = payload.size
                                )
                                serverRef?.sendMediaPacket(hdr16, payload)
                                idx++
                            }
                            Thread.sleep(16) // ~60 FPS
                        }
                    }, "Emulator-FrameFeeder").apply { isDaemon = true; start() }
                }
            },
            onStopStream = {
                streaming.set(false)
                AppLogger.info("Emulator", "OBS disconnected stream.")
                println("EMULATOR_STREAM_STOPPED")
                System.out.flush()
            },
            onControlPatch = { patch ->
                AppLogger.info("Emulator", "Applied camera control patch: zoom=${patch.zoomRatio}, iso=${patch.iso}, torch=${patch.torch}")
                println("EMULATOR_CONTROL_APPLIED zoom=${patch.zoomRatio}")
                System.out.flush()
            },
            onTallyUpdate = { tally ->
                AppLogger.info("Emulator", "Tally state updated to: $tally")
            }
        )
        serverRef = server
        server.start()
        println("EMULATOR_READY port=${server.boundPort} pin=${gate.currentPin} loadedFrames=${frames.size}")
        System.out.flush()

        Thread.sleep(durationSec * 1000L)
        streaming.set(false)
        server.stop()
        println("EMULATOR_SHUTDOWN")
        System.out.flush()
    }
}
