package com.cameraphone.app.util

import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentLinkedDeque

object AppLogger {
    private const val TAG = "CameraPhone"
    private const val MAX_LINES = 250
    private val lines = ConcurrentLinkedDeque<String>()

    @Volatile
    var onLogUpdated: ((String) -> Unit)? = null

    private fun formatTime(): String {
        return try {
            SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(Date())
        } catch (_: Throwable) {
            System.currentTimeMillis().toString()
        }
    }

    fun info(component: String, msg: String) {
        append("INFO", component, msg)
        try { Log.i(TAG, "[$component] $msg") } catch (_: Throwable) {}
    }

    fun warn(component: String, msg: String) {
        append("WARN", component, msg)
        try { Log.w(TAG, "[$component] $msg") } catch (_: Throwable) {}
    }

    fun error(component: String, msg: String, err: Throwable? = null) {
        val detail = if (err != null) "$msg (${err.javaClass.simpleName}: ${err.message})" else msg
        append("ERR ", component, detail)
        try { Log.e(TAG, "[$component] $detail", err) } catch (_: Throwable) {}
    }

    private fun append(level: String, component: String, msg: String) {
        val entry = "[${formatTime()}] [$level] [$component] $msg"
        lines.addLast(entry)
        while (lines.size > MAX_LINES) {
            lines.pollFirst()
        }
        onLogUpdated?.invoke(entry)
    }

    fun dumpText(): String = lines.joinToString("\n")

    fun dumpJsonArray(): String {
        val escaped = lines.map { line ->
            "\"" + line
                .replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\r", "")
                .replace("\n", " ") + "\""
        }
        return "[" + escaped.joinToString(",") + "]"
    }

    fun clear() {
        lines.clear()
    }
}
