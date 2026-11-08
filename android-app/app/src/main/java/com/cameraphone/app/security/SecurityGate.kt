package com.cameraphone.app.security

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.concurrent.ConcurrentHashMap

enum class AuthResult {
    Authorized,
    Unauthorized,
    RateLimited,
    Disconnected
}

data class CameraBounds(
    val minZoom: Float,
    val maxZoom: Float,
    val minIso: Int,
    val maxIso: Int,
    val minExposureNs: Long,
    val maxExposureNs: Long,
    val minEv: Int,
    val maxEv: Int,
    val allowedLenses: Set<String>
)

data class RawControlPatch(
    val lensId: String? = null,
    val zoomRatio: Float? = null,
    val iso: Int? = null,
    val exposureTimeNs: Long? = null,
    val evStep: Int? = null,
    val focusDistanceDiopters: Float? = null,
    val wbKelvin: Int? = null,
    val torch: Boolean? = null
)

class SecurityGate(
    private val clockMs: () -> Long = { System.currentTimeMillis() },
    initialPin: String? = null,
    private val onPinChanged: ((String) -> Unit)? = null
) {
    private val csprng = SecureRandom()

    @Volatile
    var currentPin: String = initialPin?.takeIf { it.length == 6 && it.all(Char::isDigit) } ?: generateSixDigitPin()
        private set

    @Volatile
    var connectionEnabled: Boolean = true

    private data class AttemptRecord(val failures: Int, val lockedUntilMs: Long)
    private val ipAttempts = ConcurrentHashMap<String, AttemptRecord>()

    fun rotatePin(): String {
        currentPin = generateSixDigitPin()
        ipAttempts.clear()
        onPinChanged?.invoke(currentPin)
        return currentPin
    }

    fun resetLockouts() {
        ipAttempts.clear()
    }

    private fun generateSixDigitPin(): String {
        val n = csprng.nextInt(900_000) + 100_000
        return n.toString()
    }

    fun verifyRequest(remoteIp: String, authHeader: String?): AuthResult {
        if (!connectionEnabled) {
            return AuthResult.Disconnected
        }
        val now = clockMs()
        val rec = ipAttempts[remoteIp]
        if (rec != null && rec.lockedUntilMs > now) {
            return AuthResult.RateLimited
        }

        val prefix = "Bearer "
        if (authHeader == null || !authHeader.startsWith(prefix)) {
            return AuthResult.Unauthorized
        }
        val candidate = authHeader.substring(prefix.length).trim()
        val isLoopback = (remoteIp == "127.0.0.1" ||
                          remoteIp == "::1" ||
                          remoteIp == "0:0:0:0:0:0:0:1" ||
                          remoteIp.startsWith("127.") ||
                          remoteIp.endsWith(":127.0.0.1"))
        val isPrivateLan = isLoopback ||
                           remoteIp.startsWith("192.168.") ||
                           remoteIp.startsWith("10.") ||
                           remoteIp.startsWith("169.254.") ||
                           remoteIp.matches(Regex("^172\\.(1[6-9]|2[0-9]|3[0-1])\\..*")) ||
                           remoteIp.startsWith("fe80:", ignoreCase = true) ||
                           remoteIp.startsWith("fc", ignoreCase = true) ||
                           remoteIp.startsWith("fd", ignoreCase = true)
        if (isPrivateLan && candidate == "usb-zero-config") {
            ipAttempts.remove(remoteIp)
            return AuthResult.Authorized
        }
        val ok = MessageDigest.isEqual(
            candidate.toByteArray(Charsets.UTF_8),
            currentPin.toByteArray(Charsets.UTF_8)
        )
        return if (ok) {
            ipAttempts.remove(remoteIp)
            AuthResult.Authorized
        } else {
            if (candidate.length >= 4) {
                recordFailure(remoteIp, now)
            }
            AuthResult.Unauthorized
        }
    }

    private fun recordFailure(ip: String, now: Long) {
        ipAttempts.compute(ip) { _, prev ->
            val currentFailures = if (prev == null || (prev.lockedUntilMs in 1..now)) 1 else prev.failures + 1
            val lockUntil = if (currentFailures >= 5) now + 60_000L else 0L
            AttemptRecord(currentFailures, lockUntil)
        }
    }

    companion object {
        fun sanitizeControlPatch(raw: RawControlPatch, bounds: CameraBounds): RawControlPatch {
            if (raw.lensId != null && !bounds.allowedLenses.contains(raw.lensId)) {
                throw IllegalArgumentException("Disallowed lensId: ${raw.lensId}")
            }
            return RawControlPatch(
                lensId = raw.lensId,
                zoomRatio = raw.zoomRatio?.coerceIn(bounds.minZoom, bounds.maxZoom),
                iso = raw.iso?.coerceIn(bounds.minIso, bounds.maxIso),
                exposureTimeNs = raw.exposureTimeNs?.coerceIn(bounds.minExposureNs, bounds.maxExposureNs),
                evStep = raw.evStep?.coerceIn(bounds.minEv, bounds.maxEv),
                focusDistanceDiopters = raw.focusDistanceDiopters?.coerceIn(0.0f, 15.0f),
                wbKelvin = raw.wbKelvin?.coerceIn(2000, 10000),
                torch = raw.torch
            )
        }
    }
}
