package com.cameraphone.app.security

import com.cameraphone.app.net.CamPhoneServer
import org.junit.Assert.*
import org.junit.Test
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.Socket

class SecurityGateTest {

    @Test
    fun `generates 6 digit pin and validates in constant time`() {
        val gate = SecurityGate(clockMs = { 1000L })
        val pin = gate.currentPin
        assertEquals(6, pin.length)
        assertTrue(pin.all { it.isDigit() })

        assertEquals(AuthResult.Authorized, gate.verifyRequest("192.168.1.50", "Bearer $pin"))
        assertEquals(AuthResult.Unauthorized, gate.verifyRequest("192.168.1.50", "Bearer 000000"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("192.168.1.50", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("10.0.0.12", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("172.16.5.4", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Unauthorized, gate.verifyRequest("203.0.113.50", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("127.0.0.1", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("::1", "Bearer usb-zero-config"))
        assertEquals(AuthResult.Authorized, gate.verifyRequest("::ffff:127.0.0.1", "Bearer usb-zero-config"))
    }

    @Test
    fun `locks out ip after 5 failed attempts for 60 seconds`() {
        var now = 10_000L
        val gate = SecurityGate(clockMs = { now })
        val ip = "192.168.1.99"

        repeat(5) {
            assertEquals(AuthResult.Unauthorized, gate.verifyRequest(ip, "Bearer wrong"))
        }
        // 6th attempt even with VALID PIN is blocked during 60s window
        assertEquals(AuthResult.RateLimited, gate.verifyRequest(ip, "Bearer ${gate.currentPin}"))

        // Advance clock by 61 seconds -> unlocked
        now += 61_000L
        assertEquals(AuthResult.Authorized, gate.verifyRequest(ip, "Bearer ${gate.currentPin}"))
    }

    @Test
    fun `sanitizes and clamps camera control patch within hardware bounds`() {
        val bounds = CameraBounds(
            minZoom = 1.0f, maxZoom = 10.0f,
            minIso = 50, maxIso = 6400,
            minExposureNs = 100_000L, maxExposureNs = 200_000_000L,
            minEv = -12, maxEv = 12,
            allowedLenses = setOf("0", "2", "3")
        )
        val raw = RawControlPatch(
            lensId = "2",
            zoomRatio = 99.0f, // out of bounds -> clamp to 10.0f
            iso = 10,          // out of bounds -> clamp to 50
            exposureTimeNs = 500_000_000L, // clamp to 200_000_000L
            evStep = -50,      // clamp to -12
            focusDistanceDiopters = -3.0f, // clamp to 0.0f
            wbKelvin = 15000,  // clamp to 10000
            torch = true
        )
        val clean = SecurityGate.sanitizeControlPatch(raw, bounds)
        assertEquals("2", clean.lensId)
        assertEquals(10.0f, clean.zoomRatio ?: 0f, 0.001f)
        assertEquals(50, clean.iso)
        assertEquals(200_000_000L, clean.exposureTimeNs)
        assertEquals(-12, clean.evStep)
        assertEquals(0.0f, clean.focusDistanceDiopters ?: -1f, 0.001f)
        assertEquals(10000, clean.wbKelvin)
        assertEquals(true, clean.torch)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `rejects unknown lens id injection`() {
        val bounds = CameraBounds(1f, 8f, 100, 3200, 1000L, 1000000L, -6, 6, setOf("0", "2"))
        SecurityGate.sanitizeControlPatch(RawControlPatch(lensId = "0; rm -rf /"), bounds)
    }

    @Test
    fun `server rejects unauthenticated requests with 401 and allows authorized capabilities fetch`() {
        val gate = SecurityGate()
        val server = CamPhoneServer(
            port = 0, // ephemeral port
            usbOnlyLoopback = true,
            securityGate = gate,
            capabilitiesJsonProvider = { "{\"deviceModel\":\"TestPhone\"}" },
            onStartStream = { _, _, _, _, _, _ -> },
            onStopStream = {},
            onControlPatch = {},
            onTallyUpdate = {}
        )
        server.start()
        val port = server.boundPort
        try {
            // 1. Unauthenticated request -> HTTP 401
            Socket("127.0.0.1", port).use { s ->
                s.getOutputStream().write("GET /v1/capabilities HTTP/1.1\r\nHost: localhost\r\n\r\n".toByteArray())
                val statusLine = BufferedReader(InputStreamReader(s.getInputStream())).readLine()
                assertTrue(statusLine.contains("401"))
            }

            // 2. Authenticated request -> HTTP 200 + JSON
            Socket("127.0.0.1", port).use { s ->
                val req = "GET /v1/capabilities HTTP/1.1\r\nAuthorization: Bearer ${gate.currentPin}\r\n\r\n"
                s.getOutputStream().write(req.toByteArray())
                val reader = BufferedReader(InputStreamReader(s.getInputStream()))
                val statusLine = reader.readLine()
                assertTrue(statusLine.contains("200"))
            }

            // 3. Zero-Config USB Loopback request (Bearer usb-zero-config on 127.0.0.1) -> HTTP 200 + JSON
            Socket("127.0.0.1", port).use { s ->
                val req = "GET /v1/capabilities HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n"
                s.getOutputStream().write(req.toByteArray())
                val reader = BufferedReader(InputStreamReader(s.getInputStream()))
                val statusLine = reader.readLine()
                assertTrue(statusLine.contains("200"))
            }

            // 4. Wi-Fi LAN IP attempting Bearer usb-zero-config -> Authorized (Zero-Config LAN), public WAN IP -> Unauthorized
            assertEquals(AuthResult.Authorized, gate.verifyRequest("192.168.1.50", "Bearer usb-zero-config"))
            assertEquals(AuthResult.Unauthorized, gate.verifyRequest("8.8.8.8", "Bearer usb-zero-config"))

            // 5. Integrated Diagnostic Logs endpoint (/v1/logs) -> HTTP 200 + JSON logs array
            Socket("127.0.0.1", port).use { s ->
                val req = "GET /v1/logs HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n"
                s.getOutputStream().write(req.toByteArray())
                val reader = BufferedReader(InputStreamReader(s.getInputStream()))
                val statusLine = reader.readLine()
                assertTrue(statusLine.contains("200"))
                val fullResp = reader.readText()
                assertTrue(fullResp.contains("\"logs\":["))
            }
        } finally {
            server.stop()
        }
    }

    @Test
    fun `replacement stream does not get stopped when evicted stream closes`() {
        val gate = SecurityGate()
        var activeStreams = 0
        val server = CamPhoneServer(
            port = 0,
            usbOnlyLoopback = true,
            securityGate = gate,
            capabilitiesJsonProvider = { "{}" },
            onStartStream = { _, _, _, _, _, _ -> activeStreams++ },
            onStopStream = { activeStreams-- },
            onControlPatch = {},
            onTallyUpdate = {}
        )
        server.start()
        val port = server.boundPort
        try {
            val s1 = Socket("127.0.0.1", port)
            s1.getOutputStream().write("GET /v1/stream?lens=0&res=1920x1080&fps=60&codec=hevc&hdr=0 HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n".toByteArray())
            val r1 = BufferedReader(InputStreamReader(s1.getInputStream()))
            assertTrue(r1.readLine().contains("200"))
            while (r1.readLine().isNotEmpty()) {}
            Thread.sleep(50)
            assertEquals(1, activeStreams)

            // Connect s2 replacing s1
            val s2 = Socket("127.0.0.1", port)
            s2.getOutputStream().write("GET /v1/stream?lens=0&res=1920x1080&fps=60&codec=hevc&hdr=0 HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n".toByteArray())
            val r2 = BufferedReader(InputStreamReader(s2.getInputStream()))
            assertTrue(r2.readLine().contains("200"))
            while (r2.readLine().isNotEmpty()) {}

            // Wait for s1 handler thread's finally block to finish after s1 closes
            s1.close()
            Thread.sleep(100)

            // s2 must still own the active stream (activeStreams == 1) and receive media packets!
            assertEquals("Evicted stream s1 must not stop s2's active stream", 1, activeStreams)
            server.sendMediaPacket(ByteArray(16) { 1 }, ByteArray(4) { 2 })
            val received = ByteArray(20)
            var total = 0
            s2.soTimeout = 1000
            while (total < 20) {
                val n = s2.getInputStream().read(received, total, 20 - total)
                if (n <= 0) break
                total += n
            }
            assertEquals(20, total)
            s2.close()
        } finally {
            server.stop()
        }
    }

    @Test
    fun `kill switch blocks all requests when disconnected and severs active stream immediately`() {
        val gate = SecurityGate()
        assertTrue(gate.connectionEnabled)

        var activeStreams = 0
        val server = CamPhoneServer(
            port = 0,
            usbOnlyLoopback = true,
            securityGate = gate,
            capabilitiesJsonProvider = { "{}" },
            onStartStream = { _, _, _, _, _, _ -> activeStreams++ },
            onStopStream = { activeStreams-- },
            onControlPatch = {},
            onTallyUpdate = {}
        )
        server.start()
        val port = server.boundPort
        try {
            // 1. Start an active stream
            val streamSock = Socket("127.0.0.1", port)
            streamSock.soTimeout = 1000
            streamSock.getOutputStream().write("GET /v1/stream?lens=0&res=1920x1080&fps=30&codec=hevc&hdr=0 HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n".toByteArray())
            val reader = BufferedReader(InputStreamReader(streamSock.getInputStream()))
            assertTrue(reader.readLine().contains("200"))
            while (reader.readLine().isNotEmpty()) {}
            Thread.sleep(50)
            assertEquals(1, activeStreams)

            // 2. Disarm SecurityGate -> verifyRequest returns AuthResult.Disconnected even for valid PIN or usb-zero-config
            gate.connectionEnabled = false
            assertEquals(AuthResult.Disconnected, gate.verifyRequest("127.0.0.1", "Bearer usb-zero-config"))
            assertEquals(AuthResult.Disconnected, gate.verifyRequest("192.168.1.50", "Bearer ${gate.currentPin}"))

            // 3. Server rejects new requests with 503 when disarmed
            Socket("127.0.0.1", port).use { s ->
                s.getOutputStream().write("GET /v1/capabilities HTTP/1.1\r\nAuthorization: Bearer usb-zero-config\r\n\r\n".toByteArray())
                val statusLine = BufferedReader(InputStreamReader(s.getInputStream())).readLine()
                assertTrue("Expected 503 when disconnected, got: $statusLine", statusLine.contains("503"))
            }

            // 4. Calling server.stop() immediately severs the active stream socket and triggers onStopStream()
            server.stop()
            val eofOrClosed = try {
                streamSock.getInputStream().read() == -1
            } catch (_: Exception) {
                true
            }
            assertTrue("Active stream socket must be immediately closed on server.stop()", eofOrClosed)
            Thread.sleep(50)
            assertEquals("onStopStream must be called when server stops active stream", 0, activeStreams)
            streamSock.close()

            // 5. Re-arming SecurityGate restores normal authorization
            gate.connectionEnabled = true
            assertEquals(AuthResult.Authorized, gate.verifyRequest("127.0.0.1", "Bearer usb-zero-config"))
        } finally {
            server.stop()
        }
    }
}
