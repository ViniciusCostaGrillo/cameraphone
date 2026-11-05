package com.cameraphone.obs.security

import com.cameraphone.obs.net.CamPhoneServer
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

            // 4. Wi-Fi LAN IP attempting Bearer usb-zero-config -> MUST be rejected (Unauthorized)
            assertEquals(AuthResult.Unauthorized, gate.verifyRequest("192.168.1.50", "Bearer usb-zero-config"))
        } finally {
            server.stop()
        }
    }
}
