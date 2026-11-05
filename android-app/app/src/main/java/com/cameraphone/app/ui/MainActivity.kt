package com.cameraphone.app.ui

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
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.cameraphone.app.R
import com.cameraphone.app.service.CameraStreamService
import com.cameraphone.app.util.AppLogger
import java.net.Inet4Address
import java.net.NetworkInterface

enum class AppLanguage { PT, EN }

class MainActivity : Activity() {

    private var streamService: CameraStreamService? = null
    private var currentLanguage = AppLanguage.PT
    private var currentTally = "idle"
    private var isStreaming = false
    private var lastTelemetryInfo = ""

    private lateinit var langToggleBtn: Button
    private lateinit var tallyBanner: TextView
    private lateinit var ipInfoText: TextView
    private lateinit var pinLabelText: TextView
    private lateinit var pinValueText: TextView
    private lateinit var rotatePinBtn: Button
    private lateinit var telemetryText: TextView
    private lateinit var usbOnlySwitch: Switch
    private lateinit var logTitleText: TextView
    private lateinit var copyLogBtn: Button
    private lateinit var blackoutBtn: Button
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

        val prefs = getSharedPreferences("cameraphone_prefs", Context.MODE_PRIVATE)
        val savedLang = prefs.getString("language", "PT") ?: "PT"
        currentLanguage = if (savedLang == "EN") AppLanguage.EN else AppLanguage.PT

        buildMinimalistOledUi()
        applyLanguageStrings()
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
            if (currentLanguage == AppLanguage.PT) "Sem Wi-Fi (Use Cabo USB)" else "No Wi-Fi (Use USB Cable)"
        } catch (_: Throwable) {
            if (currentLanguage == AppLanguage.PT) "Desconhecido" else "Unknown"
        }
    }

    private fun updateIpBanner(usbOnly: Boolean) {
        val ip = detectWifiIpv4()
        ipInfoText.text = if (usbOnly) {
            if (currentLanguage == AppLanguage.PT) {
                "🔌 MODO EXCLUSIVO CABO USB (127.0.0.1:4748) • Wi-Fi Bloqueado"
            } else {
                "🔌 EXCLUSIVE USB CABLE MODE (127.0.0.1:4748) • Wi-Fi Blocked"
            }
        } else {
            if (currentLanguage == AppLanguage.PT) {
                "📶 IP Wi-Fi: $ip:4748  |  🔌 Cabo USB: 127.0.0.1:4748"
            } else {
                "📶 Wi-Fi IP: $ip:4748  |  🔌 USB Cable: 127.0.0.1:4748"
            }
        }
    }

    private fun applyLanguageStrings() {
        langToggleBtn.text = if (currentLanguage == AppLanguage.PT) "🇧🇷 PT" else "🇺🇸 EN"
        updateIpBanner(usbOnlySwitch.isChecked)
        pinLabelText.text = if (currentLanguage == AppLanguage.PT) {
            "PIN WI-FI ZERO-TRUST (No Cabo USB a conexão é automática sem PIN)"
        } else {
            "ZERO-TRUST WI-FI PIN (USB Cable connects automatically without PIN)"
        }
        rotatePinBtn.text = if (currentLanguage == AppLanguage.PT) {
            "Gerar Novo PIN (Desconectar Outros)"
        } else {
            "Generate New PIN (Disconnect Others)"
        }
        usbOnlySwitch.text = if (currentLanguage == AppLanguage.PT) {
            "Bloquear Wi-Fi (Permitir Somente Cabo USB)"
        } else {
            "Block Wi-Fi (Allow USB Cable Only)"
        }
        logTitleText.text = if (currentLanguage == AppLanguage.PT) {
            "📋 LOGS DE DIAGNÓSTICO EM TEMPO REAL"
        } else {
            "📋 REAL-TIME DIAGNOSTIC LOGS"
        }
        copyLogBtn.text = if (currentLanguage == AppLanguage.PT) "Copiar Logs" else "Copy Logs"
        blackoutBtn.text = if (currentLanguage == AppLanguage.PT) {
            "Modo Tela Preta OLED (Duplo Toque p/ Sair)"
        } else {
            "OLED Blackout Mode (Double Tap to Exit)"
        }
        updateStatusUi(currentTally, isStreaming, lastTelemetryInfo)
    }

    private fun toggleLanguage() {
        currentLanguage = if (currentLanguage == AppLanguage.PT) AppLanguage.EN else AppLanguage.PT
        getSharedPreferences("cameraphone_prefs", Context.MODE_PRIVATE)
            .edit()
            .putString("language", currentLanguage.name)
            .apply()
        applyLanguageStrings()
        val msg = if (currentLanguage == AppLanguage.PT) "Idioma alterado para Português" else "Language changed to English"
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
    }

    private fun refreshLogsUi() {
        if (!::logConsoleText.isInitialized) return
        val emptyMsg = if (currentLanguage == AppLanguage.PT) "[Log] Aguardando eventos..." else "[Log] Waiting for events..."
        logConsoleText.text = AppLogger.dumpText().ifEmpty { emptyMsg }
        logScrollView.post { logScrollView.fullScroll(View.FOCUS_DOWN) }
    }

    private fun buildMinimalistOledUi() {
        val density = resources.displayMetrics.density
        fun dp(v: Int): Int = (v * density).toInt()

        rootContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#0D0F12"))
            setPadding(dp(18), dp(18), dp(18), dp(18))
        }

        // 0. Brand Header with CameraPhone Icon and Language Toggle Button
        val brandRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, 0, 0, dp(10))
        }
        val logoImg = ImageView(this).apply {
            setImageResource(R.mipmap.ic_launcher)
            layoutParams = LinearLayout.LayoutParams(dp(40), dp(40)).apply { rightMargin = dp(10) }
        }
        val brandTitle = TextView(this).apply {
            text = "CameraPhone 4K Pro"
            setTextColor(Color.WHITE)
            textSize = 19f
            typeface = Typeface.DEFAULT_BOLD
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        langToggleBtn = Button(this).apply {
            text = "🇧🇷 PT"
            textSize = 11f
            setTextColor(Color.parseColor("#38BDF8"))
            typeface = Typeface.DEFAULT_BOLD
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#1E293B"))
                cornerRadius = dp(8).toFloat()
                setStroke(dp(1), Color.parseColor("#0284C7"))
            }
            setPadding(dp(10), dp(4), dp(10), dp(4))
            setOnClickListener { toggleLanguage() }
        }

        brandRow.addView(logoImg)
        brandRow.addView(brandTitle)
        brandRow.addView(langToggleBtn)
        rootContainer.addView(brandRow)

        // 1. Tally Status Banner
        tallyBanner = TextView(this).apply {
            text = "● AGUARDANDO CONEXÃO (PORTA 4748)"
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
        pinLabelText = TextView(this).apply {
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
        rotatePinBtn = Button(this).apply {
            text = "Gerar Novo PIN (Desconectar Outros)"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#262B33"))
            setOnClickListener {
                val newPin = streamService?.securityGate?.rotatePin() ?: return@setOnClickListener
                pinValueText.text = newPin.chunked(3).joinToString(" ")
                AppLogger.info("UI", "PIN rotated by user")
            }
        }
        pinCard.addView(pinLabelText)
        pinCard.addView(pinValueText)
        pinCard.addView(rotatePinBtn)

        rootContainer.addView(pinCard, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(8) })

        // 4. Transport Security Switch
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
            text = "Hardware Pronto • Sem Marca D'água • Controle Remoto 100%"
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
        logTitleText = TextView(this).apply {
            text = "📋 LOGS DE DIAGNÓSTICO EM TEMPO REAL"
            setTextColor(Color.parseColor("#8AB4F8"))
            textSize = 11f
            typeface = Typeface.DEFAULT_BOLD
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        copyLogBtn = Button(this).apply {
            text = "Copiar Logs"
            textSize = 10f
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#262B33"))
            setOnClickListener {
                val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                cm.setPrimaryClip(ClipData.newPlainText("CameraPhone-Logs", AppLogger.dumpText()))
                val msg = if (currentLanguage == AppLanguage.PT) "Logs copiados!" else "Logs copied!"
                Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show()
            }
        }
        logHeaderRow.addView(logTitleText)
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
        blackoutBtn = Button(this).apply {
            text = "Modo Tela Preta OLED (Duplo Toque p/ Sair)"
            setTextColor(Color.parseColor("#00E676"))
            setBackgroundColor(Color.parseColor("#16191E"))
            setOnClickListener { enableBlackoutOverlay() }
        }
        rootContainer.addView(blackoutBtn)

        setContentView(rootContainer)
    }

    private fun updateStatusUi(tally: String, streaming: Boolean, info: String) {
        currentTally = tally
        isStreaming = streaming
        lastTelemetryInfo = info

        if (!streaming) {
            tallyBanner.text = if (currentLanguage == AppLanguage.PT) {
                "● AGUARDANDO CONEXÃO (PORTA 4748)"
            } else {
                "● WAITING FOR CONNECTION (PORT 4748)"
            }
            tallyBanner.setTextColor(Color.parseColor("#9AA0A6"))
            telemetryText.text = if (info.isNotEmpty()) info else (
                if (currentLanguage == AppLanguage.PT) "Standby • Nenhum cliente conectado" else "Standby • No client connected"
            )
            return
        }
        if (info.isNotEmpty()) {
            telemetryText.text = info
        }
        when (tally.lowercase()) {
            "program" -> {
                tallyBanner.text = if (currentLanguage == AppLanguage.PT) "● NO AR (TRANSMISSÃO ATIVA)" else "● ON AIR (LIVE STREAMING)"
                tallyBanner.setTextColor(Color.parseColor("#FF1744"))
            }
            "preview" -> {
                tallyBanner.text = if (currentLanguage == AppLanguage.PT) "● EM PREVIEW" else "● IN PREVIEW"
                tallyBanner.setTextColor(Color.parseColor("#00E676"))
            }
            else -> {
                tallyBanner.text = if (currentLanguage == AppLanguage.PT) "● TRANSMITINDO VÍDEO" else "● STREAMING VIDEO"
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
