package com.cameraphone.obs.ui

import android.Manifest
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.IBinder
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.cameraphone.obs.service.CameraStreamService
import com.cameraphone.obs.util.AppLogger
import java.net.Inet4Address
import java.net.NetworkInterface

class MainActivity : Activity() {

    private var streamService: CameraStreamService? = null
    private lateinit var tallyBanner: TextView
    private lateinit var ipInfoText: TextView
    private lateinit var pinValueText: TextView
    private lateinit var telemetryText: TextView
    private lateinit var usbOnlySwitch: Switch
    private lateinit var logConsoleText: TextView
    private lateinit var logScrollView: ScrollView
    private lateinit var rootContainer: LinearLayout
    private var blackoutOverlay: View? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val svc = (binder as? CameraStreamService.LocalBinder)?.getService() ?: return
            streamService = svc
            pinValueText.text = svc.securityGate.currentPin.chunked(3).joinToString(" ")
            usbOnlySwitch.isChecked = svc.usbOnlyMode
            updateIpBanner(svc.usbOnlyMode)
            refreshLogsUi()
            svc.onUiStateChange = { tally, streaming, info ->
                runOnUiThread { updateStatusUi(tally, streaming, info) }
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            streamService = null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        buildMinimalistOledUi()
        requestNecessaryPermissions()

        AppLogger.onLogUpdated = {
            runOnUiThread { refreshLogsUi() }
        }

        val intent = Intent(this, CameraStreamService::class.java)
        startForegroundService(intent)
        bindService(intent, connection, Context.BIND_AUTO_CREATE)
    }

    private fun detectWifiIpv4(): String {
        return try {
            val interfaces = NetworkInterface.getNetworkInterfaces()?.toList().orEmpty()
            for (nif in interfaces) {
                if (!nif.isUp || nif.isLoopback) continue
                for (addr in nif.inetAddresses) {
                    if (addr is Inet4Address && !addr.isLoopbackAddress) {
                        val ip = addr.hostAddress ?: continue
                        if (ip.startsWith("192.") || ip.startsWith("10.") || ip.startsWith("172.")) {
                            return ip
                        }
                    }
                }
            }
            "Sem Wi-Fi (Use Cabo USB)"
        } catch (_: Throwable) {
            "Desconhecido"
        }
    }

    private fun updateIpBanner(usbOnly: Boolean) {
        val ip = detectWifiIpv4()
        ipInfoText.text = if (usbOnly) {
            "🔌 MODO EXCLUSIVO CABO USB (127.0.0.1:4748) • Wi-Fi Bloqueado"
        } else {
            "📶 IP Wi-Fi: $ip:4748  |  🔌 Cabo USB: 127.0.0.1:4748"
        }
    }

    private fun refreshLogsUi() {
        if (!::logConsoleText.isInitialized) return
        logConsoleText.text = AppLogger.dumpText().ifEmpty { "[Log] Aguardando eventos..." }
        logScrollView.post { logScrollView.fullScroll(View.FOCUS_DOWN) }
    }

    private fun buildMinimalistOledUi() {
        val density = resources.displayMetrics.density
        fun dp(v: Int): Int = (v * density).toInt()

        rootContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#0D0F12"))
            setPadding(dp(18), dp(24), dp(18), dp(18))
        }

        // 1. Tally Status Banner
        tallyBanner = TextView(this).apply {
            text = "● AGUARDANDO CONEXÃO OBS (PORTA 4748)"
            setTextColor(Color.parseColor("#9AA0A6"))
            textSize = 13f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            gravity = Gravity.CENTER
            setPadding(dp(12), dp(10), dp(12), dp(10))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#16191E"))
                cornerRadius = dp(10).toFloat()
                setStroke(dp(1), Color.parseColor("#262B33"))
            }
        }
        rootContainer.addView(tallyBanner, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ))

        // 2. IP & Transport Info Banner
        ipInfoText = TextView(this).apply {
            text = "📶 IP Wi-Fi: ${detectWifiIpv4()}:4748  |  🔌 Cabo USB: 127.0.0.1:4748"
            setTextColor(Color.parseColor("#00E676"))
            textSize = 12f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            gravity = Gravity.CENTER
            setPadding(dp(8), dp(8), dp(8), dp(8))
        }
        rootContainer.addView(ipInfoText)

        // 3. Security Pairing Card (6-Digit PIN + Rotate Button)
        val pinCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(16), dp(18), dp(16))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#16191E"))
                cornerRadius = dp(14).toFloat()
                setStroke(dp(1), Color.parseColor("#2E3440"))
            }
        }
        val pinLabel = TextView(this).apply {
            text = "PIN WI-FI ZERO-TRUST (No Cabo USB a conexão é automática sem PIN)"
            setTextColor(Color.parseColor("#8AB4F8"))
            textSize = 10f
            typeface = Typeface.DEFAULT_BOLD
        }
        pinValueText = TextView(this).apply {
            text = "--- ---"
            setTextColor(Color.WHITE)
            textSize = 32f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setPadding(0, dp(4), 0, dp(8))
        }
        val rotateBtn = Button(this).apply {
            text = "Gerar Novo PIN (Desconectar Outros)"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#262B33"))
            setOnClickListener {
                val newPin = streamService?.securityGate?.rotatePin() ?: return@setOnClickListener
                pinValueText.text = newPin.chunked(3).joinToString(" ")
                AppLogger.info("UI", "PIN rotated by user")
            }
        }
        pinCard.addView(pinLabel)
        pinCard.addView(pinValueText)
        pinCard.addView(rotateBtn)

        rootContainer.addView(pinCard, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8) })

        // 4. Transport Security Switch (Default OFF so both USB & Wi-Fi work immediately!)
        usbOnlySwitch = Switch(this).apply {
            text = "Bloquear Wi-Fi (Permitir Somente Cabo USB)"
            setTextColor(Color.WHITE)
            textSize = 13f
            isChecked = false
            setPadding(dp(4), dp(10), dp(4), dp(10))
            setOnCheckedChangeListener { _, isChecked ->
                streamService?.restartServer(usbOnly = isChecked)
                updateIpBanner(isChecked)
            }
        }
        rootContainer.addView(usbOnlySwitch)

        // 5. Active Hardware Pipeline Telemetry
        telemetryText = TextView(this).apply {
            text = "Hardware Pronto • Sem Marca D'água • Controle 100% no OBS"
            setTextColor(Color.parseColor("#9AA0A6"))
            textSize = 12f
            typeface = Typeface.MONOSPACE
            setPadding(dp(4), dp(4), dp(4), dp(8))
        }
        rootContainer.addView(telemetryText)

        // 6. Integrated Live Diagnostic Log Console inside APK
        val logHeaderRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val logTitle = TextView(this).apply {
            text = "📋 LOGS DE DIAGNÓSTICO EM TEMPO REAL"
            setTextColor(Color.parseColor("#8AB4F8"))
            textSize = 11f
            typeface = Typeface.DEFAULT_BOLD
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        val copyLogBtn = Button(this).apply {
            text = "Copiar Logs"
            textSize = 10f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#262B33"))
            setOnClickListener {
                val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("CameraPhoneOBS-Logs", AppLogger.dumpText()))
                Toast.makeText(this@MainActivity, "Logs copiados!", Toast.LENGTH_SHORT).show()
            }
        }
        logHeaderRow.addView(logTitle)
        logHeaderRow.addView(copyLogBtn)
        rootContainer.addView(logHeaderRow)

        logConsoleText = TextView(this).apply {
            text = "[Log] Inicializando..."
            setTextColor(Color.parseColor("#CFD8DC"))
            textSize = 10f
            typeface = Typeface.MONOSPACE
            setPadding(dp(10), dp(8), dp(10), dp(8))
        }
        logScrollView = ScrollView(this).apply {
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#111317"))
                cornerRadius = dp(8).toFloat()
                setStroke(dp(1), Color.parseColor("#262B33"))
            }
            addView(logConsoleText)
        }
        rootContainer.addView(logScrollView, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            0,
            1f
        ).apply { topMargin = dp(6); bottomMargin = dp(8) })

        // 7. OLED Blackout Button
        val blackoutBtn = Button(this).apply {
            text = "Modo Tela Preta OLED (Duplo Toque p/ Sair)"
            setTextColor(Color.parseColor("#00E676"))
            setBackgroundColor(Color.parseColor("#16191E"))
            setOnClickListener { enableBlackoutOverlay() }
        }
        rootContainer.addView(blackoutBtn)

        setContentView(rootContainer)
    }

    private fun updateStatusUi(tally: String, streaming: Boolean, info: String) {
        if (!streaming) {
            tallyBanner.text = "● AGUARDANDO CONEXÃO OBS (PORTA 4748)"
            tallyBanner.setTextColor(Color.parseColor("#9AA0A6"))
            telemetryText.text = if (info.isNotEmpty()) info else "Standby • Nenhum cliente conectado"
            return
        }
        if (info.isNotEmpty()) {
            telemetryText.text = info
        }
        when (tally.lowercase()) {
            "program" -> {
                tallyBanner.text = "● NO AR (OBS PROGRAM ATIVO)"
                tallyBanner.setTextColor(Color.parseColor("#FF1744"))
            }
            "preview" -> {
                tallyBanner.text = "● EM PREVIEW NO OBS"
                tallyBanner.setTextColor(Color.parseColor("#00E676"))
            }
            else -> {
                tallyBanner.text = "● TRANSMITINDO PARA O OBS"
                tallyBanner.setTextColor(Color.parseColor("#8AB4F8"))
            }
        }
    }

    private fun enableBlackoutOverlay() {
        val overlay = View(this).apply {
            setBackgroundColor(Color.BLACK)
            var lastTap = 0L
            setOnClickListener {
                val now = System.currentTimeMillis()
                if (now - lastTap < 400L) {
                    setContentView(rootContainer)
                    blackoutOverlay = null
                }
                lastTap = now
            }
        }
        blackoutOverlay = overlay
        setContentView(overlay)
    }

    private fun requestNecessaryPermissions() {
        val perms = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        val missing = perms.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) {
            AppLogger.warn("Permissions", "Requesting permissions from user: $missing")
            requestPermissions(missing.toTypedArray(), 100)
        } else {
            AppLogger.info("Permissions", "CAMERA and RECORD_AUDIO permissions granted.")
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        for (i in permissions.indices) {
            val granted = grantResults.getOrNull(i) == PackageManager.PERMISSION_GRANTED
            AppLogger.info("Permissions", "${permissions[i]} -> ${if (granted) "GRANTED" else "DENIED"}")
        }
    }

    override fun onDestroy() {
        AppLogger.onLogUpdated = null
        try { unbindService(connection) } catch (_: Exception) {}
        super.onDestroy()
    }
}
