package com.cameraphone.obs.net

import com.cameraphone.obs.security.AuthResult
import com.cameraphone.obs.security.RawControlPatch
import com.cameraphone.obs.security.SecurityGate
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

class CamPhoneServer(
    private val port: Int = 4748,
    private val usbOnlyLoopback: Boolean = false,
    private val securityGate: SecurityGate,
    private val capabilitiesJsonProvider: () -> String,
    private val onStartStream: (lensId: String, width: Int, height: Int, fps: Int, codec: String, hdr: Boolean) -> Unit,
    private val onStopStream: () -> Unit,
    private val onControlPatch: (RawControlPatch) -> Unit,
    private val onTallyUpdate: (String) -> Unit
) {
    private var serverSocket: ServerSocket? = null
    private val running = AtomicBoolean(false)
    private val streamingActive = AtomicBoolean(false)

    @Volatile
    private var activeStreamOutput: OutputStream? = null
    private val writeLock = Any()

    val boundPort: Int
        get() = serverSocket?.localPort ?: port

    fun start() {
        if (running.getAndSet(true)) return
        val bindAddr = if (usbOnlyLoopback) InetAddress.getByName("127.0.0.1") else InetAddress.getByName("0.0.0.0")
        val ss = ServerSocket()
        ss.reuseAddress = true
        ss.bind(InetSocketAddress(bindAddr, port))
        serverSocket = ss

        Thread({
            while (running.get()) {
                val client = try {
                    ss.accept()
                } catch (_: Exception) {
                    break
                }
                Thread({ handleClient(client) }, "CamPhone-Client").apply { isDaemon = true; start() }
            }
        }, "CamPhone-Acceptor").apply { isDaemon = true; start() }
    }

    fun sendMediaPacket(header16: ByteArray, payload: ByteArray) {
        val out = activeStreamOutput ?: return
        synchronized(writeLock) {
            try {
                out.write(header16)
                out.write(payload)
                out.flush()
            } catch (_: Exception) {
                activeStreamOutput = null
                streamingActive.set(false)
                onStopStream()
            }
        }
    }

    private fun handleClient(socket: Socket) {
        socket.tcpNoDelay = true
        socket.soTimeout = 5000
        val remoteIp = socket.inetAddress?.hostAddress ?: "unknown"

        try {
            val reader = BufferedReader(InputStreamReader(socket.getInputStream(), Charsets.UTF_8))
            val out = socket.getOutputStream()

            val requestLine = reader.readLine() ?: return
            val parts = requestLine.split(" ")
            if (parts.size < 2) return
            val method = parts[0]
            val fullPath = parts[1]

            var authHeader: String? = null
            var contentLength = 0
            while (true) {
                val line = reader.readLine() ?: break
                if (line.isEmpty()) break
                if (line.startsWith("Authorization:", ignoreCase = true)) {
                    authHeader = line.substringAfter(":").trim()
                } else if (line.startsWith("Content-Length:", ignoreCase = true)) {
                    contentLength = line.substringAfter(":").trim().toIntOrNull()?.coerceIn(0, 65536) ?: 0
                }
            }

            when (securityGate.verifyRequest(remoteIp, authHeader)) {
                AuthResult.Unauthorized -> {
                    writeHttpResponse(out, 401, "Unauthorized", "{\"error\":\"invalid_pin\"}")
                    return
                }
                AuthResult.RateLimited -> {
                    writeHttpResponse(out, 429, "Too Many Requests", "{\"error\":\"rate_limited\"}")
                    return
                }
                AuthResult.Authorized -> {}
            }

            val body = if (contentLength > 0) {
                val buf = CharArray(contentLength)
                var readTotal = 0
                while (readTotal < contentLength) {
                    val r = reader.read(buf, readTotal, contentLength - readTotal)
                    if (r <= 0) break
                    readTotal += r
                }
                String(buf, 0, readTotal)
            } else ""

            val path = fullPath.substringBefore("?")
            val query = fullPath.substringAfter("?", "")

            when {
                method == "GET" && path == "/v1/capabilities" -> {
                    writeHttpResponse(out, 200, "OK", capabilitiesJsonProvider())
                }
                method == "GET" && path == "/v1/stream" -> {
                    if (!streamingActive.compareAndSet(false, true)) {
                        writeHttpResponse(out, 409, "Conflict", "{\"error\":\"stream_already_active\"}")
                        return
                    }
                    val params = parseQuery(query)
                    val lensId = params["lens"] ?: "0"
                    val res = (params["res"] ?: "3840x2160").split("x")
                    val w = res.getOrNull(0)?.toIntOrNull() ?: 3840
                    val h = res.getOrNull(1)?.toIntOrNull() ?: 2160
                    val fps = params["fps"]?.toIntOrNull() ?: 60
                    val codec = params["codec"] ?: "hevc"
                    val hdr = params["hdr"] == "1" || params["hdr"] == "true"

                    out.write("HTTP/1.1 200 OK\r\nContent-Type: application/x-camphone-stream\r\nConnection: keep-alive\r\n\r\n".toByteArray())
                    out.flush()
                    socket.soTimeout = 0 // Streaming socket stays open
                    activeStreamOutput = out
                    onStartStream(lensId, w, h, fps, codec, hdr)

                    // Block until client disconnects
                    val probeBuf = ByteArray(16)
                    while (running.get() && streamingActive.get()) {
                        val r = socket.getInputStream().read(probeBuf)
                        if (r < 0) break
                    }
                }
                method == "PATCH" && path == "/v1/control" -> {
                    val patch = RawControlPatch(
                        lensId = extractJsonString(body, "lens"),
                        zoomRatio = extractJsonDouble(body, "zoom")?.toFloat(),
                        iso = extractJsonInt(body, "iso"),
                        exposureTimeNs = extractJsonLong(body, "shutterNs"),
                        evStep = extractJsonInt(body, "ev"),
                        focusDistanceDiopters = extractJsonDouble(body, "focus")?.toFloat(),
                        wbKelvin = extractJsonInt(body, "wbKelvin"),
                        torch = extractJsonBoolean(body, "torch")
                    )
                    onControlPatch(patch)
                    writeHttpResponse(out, 200, "OK", "{\"status\":\"applied\"}")
                }
                method == "PUT" && path == "/v1/tally" -> {
                    val state = extractJsonString(body, "state") ?: "idle"
                    onTallyUpdate(state)
                    writeHttpResponse(out, 200, "OK", "{\"tally\":\"$state\"}")
                }
                else -> {
                    writeHttpResponse(out, 404, "Not Found", "{\"error\":\"not_found\"}")
                }
            }
        } catch (_: Exception) {
        } finally {
            if (activeStreamOutput != null) {
                activeStreamOutput = null
                streamingActive.set(false)
                onStopStream()
            }
            try { socket.close() } catch (_: Exception) {}
        }
    }

    private fun extractJsonString(json: String, key: String): String? {
        val m = Regex("\"$key\"\\s*:\\s*\"([^\"]+)\"").find(json) ?: return null
        return m.groupValues[1]
    }

    private fun extractJsonDouble(json: String, key: String): Double? {
        val m = Regex("\"$key\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)").find(json) ?: return null
        return m.groupValues[1].toDoubleOrNull()
    }

    private fun extractJsonInt(json: String, key: String): Int? {
        val m = Regex("\"$key\"\\s*:\\s*(-?[0-9]+)").find(json) ?: return null
        return m.groupValues[1].toIntOrNull()
    }

    private fun extractJsonLong(json: String, key: String): Long? {
        val m = Regex("\"$key\"\\s*:\\s*(-?[0-9]+)").find(json) ?: return null
        return m.groupValues[1].toLongOrNull()
    }

    private fun extractJsonBoolean(json: String, key: String): Boolean? {
        val m = Regex("\"$key\"\\s*:\\s*(true|false)").find(json) ?: return null
        return m.groupValues[1].toBooleanStrictOrNull()
    }

    private fun parseQuery(q: String): Map<String, String> =
        q.split("&").mapNotNull {
            val kv = it.split("=", limit = 2)
            if (kv.size == 2) kv[0] to kv[1] else null
        }.toMap()

    private fun writeHttpResponse(out: OutputStream, code: Int, reason: String, jsonBody: String) {
        val bytes = jsonBody.toByteArray(Charsets.UTF_8)
        val header = "HTTP/1.1 $code $reason\r\nContent-Type: application/json\r\nContent-Length: ${bytes.size}\r\nConnection: close\r\n\r\n"
        out.write(header.toByteArray(Charsets.UTF_8))
        out.write(bytes)
        out.flush()
    }

    fun stop() {
        running.set(false)
        streamingActive.set(false)
        try { serverSocket?.close() } catch (_: Exception) {}
        serverSocket = null
    }
}
