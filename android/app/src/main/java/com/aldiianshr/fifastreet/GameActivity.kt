package com.aldiianshr.fifastreet

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import java.io.File

/** Game information, compatibility status, actions and per-game settings. */
class GameActivity : Activity() {
    private lateinit var id: String
    private lateinit var body: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        id = intent.getStringExtra("id") ?: run { finish(); return }
        body = Ui.vbox(this, 14)
        setContentView(Ui.scroll(this, body))
        GameLibrary.listener = { render() }
    }

    override fun onResume() {
        super.onResume()
        GameLibrary.listener = { render() }
        render()
    }

    private fun entry() = synchronized(GameLibrary.games) { GameLibrary.games.firstOrNull { it.id == id } }

    private fun render() {
        val e = entry() ?: run { finish(); return }
        body.removeAllViews()
        body.addView(Ui.header(this, e.name, "Kembali" to { finish() }))
        val (label, color) = GameLibrary.overall(e)
        val top = Ui.card(this)
        val row = Ui.hbox(this)
        row.addView(Ui.badge(this, label, color))
        row.addView(Ui.text(this, "   ${e.state}", 15f))
        top.addView(row)
        top.addView(Ui.text(this, "${e.kind.label} · ${e.path}", 13f, Ui.DIM))
        val actions = Ui.hbox(this)
        val running = Sessions.gameId == e.id && Sessions.handle != 0L
        actions.addView(Ui.button(this, if (running) "Resume" else "Play", true) { play(e) }.apply {
            isEnabled = e.playable; alpha = if (e.playable) 1f else 0.4f
        })
        if (running) actions.addView(Ui.button(this, "Stop") { Sessions.stop(); render() })
        actions.addView(Ui.button(this, if (e.busy) "Menganalisis…" else "Analisis ulang") { if (!e.busy) GameLibrary.analyzeAsync(e) })
        actions.addView(Ui.button(this, "Impor kunci XEX") { importKey() })
        if (App.keyFile.exists()) actions.addView(Ui.button(this, "Hapus kunci") { App.keyFile.delete(); AppLog.i("Kunci XEX dihapus"); GameLibrary.analyzeAsync(e); render() })
        top.addView(actions)
        if (!e.playable && e.kind != GameKind.HOMEBREW)
            top.addView(Ui.text(this, "Play dinonaktifkan: game ini belum dapat dijalankan secara native (lihat status di bawah). " +
                "Aplikasi tidak memakai emulator atau simulasi gameplay sebagai pengganti.", 13f, Ui.PARTIAL))
        body.addView(top)

        val comp = Ui.card(this)
        comp.addView(Ui.section(this, "STATUS KOMPATIBILITAS"))
        GameLibrary.features(e).forEach { comp.addView(Ui.statusRow(this, it.label, it.status, it.detail)) }
        body.addView(comp)

        body.addView(settingsCard(e))
        e.analysis?.let { a -> body.addView(detailsCard(a)) }
    }

    private fun play(e: GameEntry) {
        startActivity(Intent(this, PlayActivity::class.java).putExtra("id", e.id))
    }

    private fun settingsCard(e: GameEntry): View {
        val c = Ui.card(this)
        c.addView(Ui.section(this, "PENGATURAN GAME INI"))
        val custom = Store.gameGraphics(e.id)
        c.addView(Ui.switchRow(this, "Gunakan pengaturan grafis khusus game", custom != null) { on ->
            Store.saveGameGraphics(e.id, if (on) Store.globalGraphics() else null)
            render()
        })
        if (custom != null) c.addView(GraphicsEditor.build(this, custom) { Store.saveGameGraphics(e.id, it) })
        val profiles = Store.profiles()
        val cur = Store.gameProfileName(e.id)
        c.addView(Ui.spinnerRow(this, "Profil kontrol", profiles, profiles.indexOfFirst { it.name == cur }.coerceAtLeast(0), { it.name }) {
            Store.setGameProfileName(e.id, it.name)
        })
        c.addView(Ui.button(this, "Atur kontrol untuk game ini") {
            startActivity(Intent(this, ControlsActivity::class.java).putExtra("gameId", e.id))
        })
        c.addView(Ui.text(this, "Pemetaan tombol diteruskan 1:1 sebagai tombol Xbox 360 (A/B/X/Y, LB/RB, LT/RT, D-pad, analog, Start, Back). " +
            "Fungsi gameplay FIFA Street per tombol tidak dicantumkan karena belum dapat diverifikasi dari data game.", 12f, Ui.DIM))
        return c
    }

    private fun detailsCard(a: org.json.JSONObject): View {
        val c = Ui.card(this)
        c.addView(Ui.section(this, "DETAIL EXECUTABLE"))
        fun line(k: String, v: String) = c.addView(Ui.text(this, "$k: $v", 13f))
        if (!a.optBoolean("parsed")) { line("Error", a.optString("error")); return c }
        line("Title ID / Media ID", "${a.optString("title_id")} / ${a.optString("media_id")}")
        line("Versi", a.optString("version"))
        line("PE asli", a.optString("pe_name"))
        line("Base / entry", "${a.optString("base")} / ${a.optString("entry")}")
        line("Ukuran image", GameLibrary.fmtBytes(a.optLong("image_size")))
        line("Region", a.optString("region"))
        line("Slot TLS", a.optInt("tls_slots").toString())
        a.optJSONArray("libraries")?.let { line("Library impor", (0 until it.length()).joinToString { i -> it.getString(i) }) }
        a.optJSONArray("static_libs")?.let { if (it.length() > 0) line("Static lib XDK", (0 until it.length()).joinToString { i -> it.getString(i) }) }
        a.optJSONArray("sections")?.let { s ->
            c.addView(Ui.section(this, "SECTION PE"))
            for (i in 0 until s.length()) {
                val o = s.getJSONObject(i)
                c.addView(Ui.text(this, "${o.optString("name")}  VA ${o.optString("va")}  ${GameLibrary.fmtBytes(o.optLong("size"))}${if (o.optBoolean("exec")) "  [kode]" else ""}", 13f, Ui.DIM))
            }
        }
        a.optJSONArray("imports")?.let { im ->
            c.addView(Ui.section(this, "IMPOR KERNEL (${a.optInt("imports_implemented")}/${im.length()} diimplementasikan)"))
            val sb = StringBuilder()
            for (i in 0 until im.length()) {
                val o = im.getJSONObject(i)
                sb.append(if (o.optBoolean("impl")) "✔ " else "✘ ").append(o.optString("lib")).append('!').append(o.optString("name")).append('\n')
            }
            c.addView(Ui.text(this, sb.toString().trimEnd(), 12f, Ui.DIM).apply { typeface = android.graphics.Typeface.MONOSPACE })
        }
        return c
    }

    private fun importKey() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply { addCategory(Intent.CATEGORY_OPENABLE); type = "*/*" }
        startActivityForResult(i, REQ_KEY)
    }

    @Deprecated("framework API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_KEY || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return
        val hex = String(bytes).filter { it.isLetterOrDigit() }
        val ok = bytes.size == 16 || (hex.length == 32 && hex.all { it in "0123456789abcdefABCDEF" })
        if (!ok) { Ui.info(this, "Kunci tidak valid", "File kunci harus berisi 16 byte mentah atau 32 digit heksadesimal."); return }
        File(App.keyFile.parent!!).mkdirs()
        App.keyFile.writeBytes(bytes)
        AppLog.i("Kunci XEX diimpor ke penyimpanan privat aplikasi")
        entry()?.let { GameLibrary.analyzeAsync(it) }
        render()
    }

    companion object { const val REQ_KEY = 51 }
}
