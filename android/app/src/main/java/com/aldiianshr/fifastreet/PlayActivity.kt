package com.aldiianshr.fifastreet

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView

/** Holds the single running session so it survives leaving the play screen. */
object Sessions {
    var handle: Long = 0
    var gameId: String? = null
    var userPaused = false

    fun stop() {
        if (handle != 0L) {
            NativeBridge.sessionStop(handle)
            NativeBridge.sessionDestroy(handle)
            AppLog.i("Sesi dihentikan ($gameId)")
        }
        handle = 0; gameId = null; userPaused = false
    }
}

class PlayActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var gameId: String
    private lateinit var controller: VirtualControllerView
    private lateinit var stats: TextView
    private lateinit var gamepad: Gamepad
    private lateinit var gfx: GraphicsSettings
    private val merged = PadInput()
    private val lastSent = PadInput().apply { buttons = -1 }
    private val ui = Handler(Looper.getMainLooper())
    private var thermalListener: PowerManager.OnThermalStatusChangedListener? = null
    private var errorShown = false

    private val h: Long get() = Sessions.handle

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        gameId = intent.getStringExtra("id") ?: run { finish(); return }
        val entry = synchronized(GameLibrary.games) { GameLibrary.games.firstOrNull { it.id == gameId } }
        if (Sessions.gameId != gameId) {
            Sessions.stop()
            val xex = entry?.xexPath
            if (xex == null) { fail("Executable game tidak ditemukan."); return }
            val key = if (App.keyFile.exists()) App.keyFile.path else null
            val handle = NativeBridge.sessionCreate(xex, key)
            if (handle == 0L) { fail(NativeBridge.lastCreateError()); return }
            Sessions.handle = handle
            Sessions.gameId = gameId
            AppLog.i("Sesi dibuat untuk ${entry.name}")
        }
        gfx = Store.effectiveGraphics(gameId)
        val profile = Store.profileFor(gameId)
        gamepad = Gamepad(profile)

        val root = FrameLayout(this)
        val surface = SurfaceView(this)
        surface.holder.addCallback(this)
        root.addView(surface, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        controller = VirtualControllerView(this).apply {
            this.profile = profile
            onState = { pushInput() }
        }
        root.addView(controller, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        stats = TextView(this).apply {
            setTextColor(Color.WHITE); textSize = 12f; setShadowLayer(3f, 1f, 1f, Color.BLACK)
            setPadding(Ui.dpi(this@PlayActivity, 10), Ui.dpi(this@PlayActivity, 6), 0, 0)
            visibility = if (gfx.showStats) View.VISIBLE else View.GONE
        }
        root.addView(stats, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START))
        val menu = Ui.button(this, "❚❚ Menu") { showMenu() }.apply { alpha = 0.8f }
        root.addView(menu, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        setContentView(root)
        applyGraphics()
        hideSystemUi()

        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        thermalListener = PowerManager.OnThermalStatusChangedListener { s ->
            NativeBridge.sessionSetThermal(h, s)
            if (s >= PowerManager.THERMAL_STATUS_SEVERE && gfx.thermalAdapt) AppLog.i("Thermal throttling: batas FPS diturunkan ke 30")
        }
        pm.addThermalStatusListener(thermalListener!!)
        NativeBridge.sessionSetThermal(h, pm.currentThermalStatus)
        ui.post(statsTick)
    }

    private fun fail(msg: String) {
        AppLog.i("Tidak dapat menjalankan: $msg")
        AlertDialog.Builder(this).setTitle("Tidak dapat menjalankan").setMessage(msg)
            .setPositiveButton("OK") { _, _ -> finish() }.setOnCancelListener { finish() }.show()
    }

    private fun applyGraphics() {
        NativeBridge.sessionSetGraphics(h, gfx.width, gfx.height, gfx.scalePct, gfx.fps, gfx.vsync, gfx.linear, gfx.aspect, gfx.thermalAdapt)
    }

    private fun pushInput() {
        Gamepad.merge(controller.state, gamepad.state, merged)
        if (merged.same(lastSent)) return
        lastSent.copyFrom(merged)
        NativeBridge.sessionSetPad(h, merged.buttons, merged.lt, merged.rt, merged.lx, merged.ly, merged.rx, merged.ry, true)
    }

    private val statsTick = object : Runnable {
        override fun run() {
            if (h == 0L) return
            val s = NativeBridge.sessionStats(h)
            val th = if (s[8] > 0f) "  ⚠ throttling (30 FPS)" else ""
            stats.text = "%.1f FPS  frame %.2f ms  guest %.2f ms  present %.2f ms\n%dx%d  ·  frame %d  ·  kernel calls %d  ·  %s%s"
                .format(s[0], s[1], s[2], s[3], s[4].toInt(), s[5].toInt(), s[6].toLong(), s[7].toLong(), NativeBridge.sessionGpu(h), th)
            if (!errorShown && !NativeBridge.sessionRunning(h) && NativeBridge.sessionError(h).isNotEmpty()) {
                errorShown = true
                val err = NativeBridge.sessionError(h)
                val rep = AppLog.report("runtime_error", err)
                AlertDialog.Builder(this@PlayActivity).setTitle("Runtime berhenti").setMessage("$err\n\nLaporan: ${rep.name}")
                    .setPositiveButton("Tutup") { _, _ -> Sessions.stop(); finish() }.show()
            }
            ui.postDelayed(this, 500)
        }
    }

    private fun showMenu() {
        NativeBridge.sessionPause(h)
        Sessions.userPaused = true
        val items = arrayOf(
            "Lanjutkan",
            "Statistik: ${if (gfx.showStats) "sembunyikan" else "tampilkan"}",
            "Batas FPS: ${gfx.fps} → ${if (gfx.fps == 60) 30 else 60}",
            "Resolusi internal: ${gfx.resolution}p (ganti)",
            "Filter: ${if (gfx.linear) "Linear" else "Nearest"} (ganti)",
            "Edit tata letak kontrol",
            "Kembali ke menu (sesi dijeda)",
            "Hentikan game",
        )
        AlertDialog.Builder(this).setTitle("Menu").setItems(items) { _, which ->
            when (which) {
                0 -> resumeGame()
                1 -> { gfx.showStats = !gfx.showStats; stats.visibility = if (gfx.showStats) View.VISIBLE else View.GONE; saveGfx(); resumeGame() }
                2 -> { gfx.fps = if (gfx.fps == 60) 30 else 60; saveGfx(); resumeGame() }
                3 -> { gfx.resolution = when (gfx.resolution) { 480 -> 720; 720 -> 1080; else -> 480 }; saveGfx(); resumeGame() }
                4 -> { gfx.linear = !gfx.linear; saveGfx(); resumeGame() }
                5 -> startActivity(Intent(this, ControlsActivity::class.java).putExtra("gameId", gameId))
                6 -> finish()
                7 -> { Sessions.stop(); finish() }
            }
        }.setOnCancelListener { resumeGame() }.show()
    }

    private fun saveGfx() {
        if (Store.gameGraphics(gameId) != null) Store.saveGameGraphics(gameId, gfx) else Store.saveGlobalGraphics(gfx)
        applyGraphics()
    }

    private fun resumeGame() {
        Sessions.userPaused = false
        NativeBridge.sessionResume(h)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {}

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        if (h == 0L) return
        NativeBridge.sessionSetSurface(h, holder.surface)
        if (!NativeBridge.sessionRunning(h)) NativeBridge.sessionStart(h)
        if (!Sessions.userPaused) NativeBridge.sessionResume(h)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (h != 0L) NativeBridge.sessionSetSurface(h, null)
    }

    override fun onResume() {
        super.onResume()
        if (h == 0L) return
        controller.profile = Store.profileFor(gameId)
        gamepad.profile = controller.profile
        hideSystemUi()
        if (!Sessions.userPaused) NativeBridge.sessionResume(h)
    }

    override fun onPause() {
        super.onPause()
        if (h != 0L) NativeBridge.sessionPause(h)
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacks(statsTick)
        thermalListener?.let { (getSystemService(Context.POWER_SERVICE) as PowerManager).removeThermalStatusListener(it) }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    private fun hideSystemUi() {
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (h != 0L && gamepad.onKey(event)) { pushInput(); return true }
        if (event.keyCode == KeyEvent.KEYCODE_BACK && event.action == KeyEvent.ACTION_UP) { showMenu(); return true }
        if (event.keyCode == KeyEvent.KEYCODE_BACK) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean {
        if (h != 0L && gamepad.onMotion(ev)) { pushInput(); return true }
        return super.dispatchGenericMotionEvent(ev)
    }
}
