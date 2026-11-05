package com.cameraphone.obs.ui

import android.Manifest
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
import android.widget.Switch
import android.widget.TextView
import android.app.Activity
import com.cameraphone.obs.service.CameraStreamService

class MainActivity : Activity() {

    private var streamService: CameraStreamService? = null
    private lateinit var tallyBanner: TextView
    private lateinit var pinValueText: TextView
    private lateinit var telemetryText: TextView
    private lateinit var usbOnlySwitch: Switch
    private lateinit var rootContainer: LinearLayout
    private var blackoutOverlay: View? = null

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val svc = (binder as? CameraStreamService.LocalBinder)?.getService() ?: return
            streamService = svc
            pinValueText.text = svc.securityGate.currentPin.chunked(3).joinToString(" ")
            usbOnlySwitch.isChecked = svc.usbOnlyMode
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
        requestNecessaryPermissions()
        buildMinimalistOledUi()

        val intent = Intent(this, CameraStreamService::class.java)
        startForegroundService(intent)
        bindService(intent, connection, Context.BIND_AUTO_CREATE)
    }

    private fun buildMinimalistOledUi() {
        val density = resources.displayMetrics.density
        fun dp(v: Int): Int = (v * density).toInt()

        rootContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.parseColor("#0D0F12")) // OLED Carbon Black
            setPadding(dp(24), dp(36), dp(24), dp(24))
        }

        // 1. Tally Status Banner
        tallyBanner = TextView(this).apply {
            text = "● AGUARDANDO CONEXÃO OBS (PORTA 4748)"
            setTextColor(Color.parseColor("#9AA0A6"))
            textSize = 13f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(14), dp(16), dp(14))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#16191E"))
                cornerRadius = dp(12).toFloat()
                setStroke(dp(1), Color.parseColor("#262B33"))
            }
        }
        rootContainer.addView(tallyBanner, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ))

        // 2. Security Pairing Card (6-Digit PIN + Rotate Button)
        val pinCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(28), dp(24), dp(28))
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#16191E"))
                cornerRadius = dp(16).toFloat()
                setStroke(dp(1), Color.parseColor("#2E3440"))
            }
        }
        val pinLabel = TextView(this).apply {
            text = "PIN DE SEGURANÇA ZERO-TRUST"
            setTextColor(Color.parseColor("#8AB4F8"))
            textSize = 11f
            typeface = Typeface.DEFAULT_BOLD
        }
        pinValueText = TextView(this).apply {
            text = "--- ---"
            setTextColor(Color.WHITE)
            textSize = 40f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setPadding(0, dp(8), 0, dp(16))
        }
        val rotateBtn = Button(this).apply {
            text = "Gerar Novo PIN (Desconectar Outros)"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#262B33"))
            setOnClickListener {
                val newPin = streamService?.securityGate?.rotatePin() ?: return@setOnClickListener
                pinValueText.text = newPin.chunked(3).joinToString(" ")
            }
        }
        pinCard.addView(pinLabel)
        pinCard.addView(pinValueText)
        pinCard.addView(rotateBtn)

        rootContainer.addView(pinCard, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply { topMargin = dp(20) })

        // 3. Transport Security Switch (USB Loopback Only vs Wi-Fi LAN)
        usbOnlySwitch = Switch(this).apply {
            text = "Modo Exclusivo Cabo USB (Bloqueia Wi-Fi)"
            setTextColor(Color.WHITE)
            textSize = 15f
            isChecked = true
            setPadding(dp(8), dp(20), dp(8), dp(20))
            setOnCheckedChangeListener { _, isChecked ->
                streamService?.restartServer(usbOnly = isChecked)
            }
        }
        rootContainer.addView(usbOnlySwitch)

        // 4. Active Hardware Pipeline Telemetry
        telemetryText = TextView(this).apply {
            text = "Hardware Pronto • Sem Marca D'água • Controle 100% no OBS"
            setTextColor(Color.parseColor("#9AA0A6"))
            textSize = 13f
            typeface = Typeface.MONOSPACE
            setPadding(dp(8), dp(12), dp(8), dp(24))
        }
        rootContainer.addView(telemetryText)

        // 5. OLED Blackout Button (Anti-overheating / Battery saver)
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
            telemetryText.text = "Standby • Nenhum cliente conectado"
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
            requestPermissions(missing.toTypedArray(), 100)
        }
    }

    override fun onDestroy() {
        try { unbindService(connection) } catch (_: Exception) {}
        super.onDestroy()
    }
}
