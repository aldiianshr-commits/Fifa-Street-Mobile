package com.aldiianshr.fifastreet

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.KeyEvent
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView

/** Touch layout editor, sensitivity/deadzone, gamepad remapping and profiles. */
class ControlsActivity : Activity() {
    private lateinit var pad: VirtualControllerView
    private lateinit var panel: LinearLayout
    private lateinit var profile: ControlProfile
    private var gameId: String? = null
    private var panelVisible = true

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        gameId = intent.getStringExtra("gameId")
        profile = Store.profileFor(gameId)
        val root = FrameLayout(this).apply { setBackgroundColor(0xFF0B1220.toInt()) }
        pad = VirtualControllerView(this).apply {
            editMode = true
            this.profile = this@ControlsActivity.profile
            onSelect = { buildPanel() }
            onLayoutChanged = { save() }
        }
        root.addView(pad, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        panel = Ui.vbox(this, 12).apply { background = Ui.rounded(0xE6172133.toInt(), Ui.dp(this@ControlsActivity, 12f)) }
        val sc = ScrollView(this).apply { addView(panel) }
        root.addView(sc, FrameLayout.LayoutParams(Ui.dpi(this, 360), ViewGroup.LayoutParams.MATCH_PARENT, Gravity.START).apply {
            setMargins(Ui.dpi(this@ControlsActivity, 8), Ui.dpi(this@ControlsActivity, 8), 0, Ui.dpi(this@ControlsActivity, 8))
        })
        val toggle = Ui.button(this, "☰ Panel") {
            panelVisible = !panelVisible
            sc.visibility = if (panelVisible) android.view.View.VISIBLE else android.view.View.GONE
        }
        root.addView(toggle, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.END))
        setContentView(root)
        buildPanel()
    }

    private fun save() {
        Store.upsertProfile(profile)
        if (gameId != null) Store.setGameProfileName(gameId!!, profile.name) else Store.activeProfile = profile.name
    }

    private fun switchTo(p: ControlProfile) {
        profile = p
        pad.profile = p
        save()
        buildPanel()
    }

    private fun buildPanel() {
        panel.removeAllViews()
        panel.addView(Ui.text(this, "Kontrol" + if (gameId != null) " (game ini)" else " (global)", 20f, Ui.TEXT, true))
        panel.addView(Ui.text(this, "Seret tombol untuk memindahkan. Ketuk tombol lalu atur ukurannya. Semua perubahan tersimpan otomatis.", 12f, Ui.DIM))
        val profiles = Store.profiles()
        panel.addView(Ui.spinnerRow(this, "Profil", profiles, profiles.indexOfFirst { it.name == profile.name }.coerceAtLeast(0), { it.name }) {
            if (it.name != profile.name) switchTo(it)
        })
        val row = Ui.hbox(this)
        row.addView(Ui.button(this, "Baru") { Ui.prompt(this, "Nama profil baru", "Profil ${profiles.size + 1}") { n -> if (n.isNotEmpty()) switchTo(profile.copyAs(n)) } })
        row.addView(Ui.button(this, "Ganti nama") {
            Ui.prompt(this, "Nama profil", profile.name) { n ->
                if (n.isNotEmpty()) {
                    val list = Store.profiles().filter { it.name != profile.name }.toMutableList()
                    profile.name = n; list += profile; Store.saveProfiles(list); switchTo(profile)
                }
            }
        })
        row.addView(Ui.button(this, "Hapus") {
            val list = Store.profiles().filter { it.name != profile.name }
            if (list.isEmpty()) return@button
            Store.saveProfiles(list); switchTo(list.first())
        })
        panel.addView(row)
        val presets = listOf("Default", "Ringkas", "Besar")
        panel.addView(Ui.spinnerRow(this, "Terapkan preset", listOf("—") + presets, 0, { it }) {
            if (it != "—") {
                val p = ControlProfile.preset(it).also { np -> np.name = profile.name; np.keyMap.clear(); np.keyMap.putAll(profile.keyMap) }
                switchTo(p)
            }
        })

        panel.addView(Ui.section(this, "TAMPILAN SENTUH"))
        panel.addView(Ui.seekRow(this, "Transparansi", 15, 100, (profile.opacity * 100).toInt(), { "$it%" }) { profile.opacity = it / 100f; pad.invalidate(); save() })
        val sel = pad.selectedId?.let { id -> profile.layout.firstOrNull { it.id == id } }
        if (sel != null) panel.addView(Ui.seekRow(this, "Ukuran '${sel.id}'", 50, 200, (sel.size * 100).toInt(), { "$it%" }) { sel.size = it / 100f; pad.invalidate(); save() })
        else panel.addView(Ui.text(this, "Ketuk sebuah tombol untuk mengatur ukurannya.", 13f, Ui.DIM))
        panel.addView(Ui.seekRow(this, "Sensitivitas analog sentuh", 50, 200, (profile.stickSensitivity * 100).toInt(), { "$it%" }) { profile.stickSensitivity = it / 100f; save() })
        panel.addView(Ui.switchRow(this, "Getar saat ditekan", profile.haptics) { profile.haptics = it; save() })
        panel.addView(Ui.switchRow(this, "Tampilkan kontrol sentuh", profile.showTouch) { profile.showTouch = it; save() })
        panel.addView(Ui.button(this, "Reset tata letak") {
            val d = ControlProfile.defaultLayout()
            profile.layout.clear(); profile.layout.addAll(d); pad.invalidate(); save()
        })

        panel.addView(Ui.section(this, "ANALOG & GAMEPAD"))
        panel.addView(Ui.seekRow(this, "Deadzone", 0, 50, (profile.deadzone * 100).toInt(), { "$it%" }) { profile.deadzone = it / 100f; save() })
        panel.addView(Ui.seekRow(this, "Sensitivitas gamepad", 50, 200, (profile.gamepadSensitivity * 100).toInt(), { "$it%" }) { profile.gamepadSensitivity = it / 100f; save() })
        panel.addView(Ui.switchRow(this, "Tukar analog kiri ↔ kanan", profile.swapSticks) { profile.swapSticks = it; save() })
        panel.addView(Ui.switchRow(this, "Balik sumbu Y analog kiri", profile.invertLeftY) { profile.invertLeftY = it; save() })
        panel.addView(Ui.switchRow(this, "Balik sumbu Y analog kanan", profile.invertRightY) { profile.invertRightY = it; save() })

        panel.addView(Ui.section(this, "REMAP GAMEPAD (BLUETOOTH/USB)"))
        panel.addView(Ui.text(this, "Ketuk tombol Xbox lalu tekan tombol fisik pada gamepad.", 12f, Ui.DIM))
        for (xb in XButton.values()) {
            val keys = profile.keyMap.filterValues { it == xb }.keys.joinToString { Gamepad.keyName(it) }.ifEmpty { "(tidak ada)" }
            panel.addView(Ui.button(this, "${xb.label}  ←  $keys") { capture(xb) }.apply {
                layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            })
        }
        panel.addView(Ui.button(this, "Reset remap ke default") { profile.keyMap.clear(); profile.keyMap.putAll(ControlProfile.defaultKeyMap()); save(); buildPanel() })
        panel.addView(Ui.button(this, "Selesai", true) { finish() })
    }

    private fun capture(xb: XButton) {
        val d = AlertDialog.Builder(this).setTitle("Remap ${xb.label}")
            .setMessage("Tekan tombol pada gamepad… (Back untuk batal)")
            .setNegativeButton("Batal", null).create()
        d.setOnKeyListener { dlg, code, ev ->
            if (ev.action != KeyEvent.ACTION_DOWN) return@setOnKeyListener true
            if (code == KeyEvent.KEYCODE_BACK) { dlg.dismiss(); return@setOnKeyListener true }
            profile.keyMap.entries.removeAll { it.key == code }
            profile.keyMap[code] = xb
            AppLog.i("Remap: ${Gamepad.keyName(code)} → ${xb.label}")
            save(); dlg.dismiss(); buildPanel()
            true
        }
        d.show()
        d.window?.decorView?.setBackgroundColor(Color.DKGRAY)
    }
}
