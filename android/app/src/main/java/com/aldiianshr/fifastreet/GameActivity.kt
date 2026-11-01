package com.aldiianshr.fifastreet

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import android.widget.ProgressBar
import org.json.JSONObject
import java.io.File

/** Game details: library status, every runtime requirement with its reason, disc inventory, actions. */
class GameActivity : Activity() {
    private lateinit var id: String
    private lateinit var body: LinearLayout
    private var lastRender = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        id = intent.getStringExtra("id") ?: run { finish(); return }
        body = Ui.vbox(this, 14)
        setContentView(Ui.scroll(this, body))
    }

    override fun onResume() {
        super.onResume()
        GameLibrary.listener = { throttledRender() }
        render()
    }

    private fun throttledRender() {
        val now = System.currentTimeMillis()
        val e = GameLibrary.find(id)
        if (e != null && e.processing && now - lastRender < 250) return
        render()
    }

    private fun render() {
        lastRender = System.currentTimeMillis()
        val e = GameLibrary.find(id) ?: run { finish(); return }
        body.removeAllViews()
        body.addView(Ui.header(this, e.name, "Kembali" to { finish() }))

        val top = Ui.card(this)
        val row = Ui.hbox(this)
        row.addView(Ui.badge(this, e.status.label, e.status.color))
        row.addView(Ui.text(this, "   " + if (e.status == LibStatus.PROCESSING) GameLibrary.stageLabel(e) else e.reason, 15f))
        top.addView(row)
        if (e.status == LibStatus.PROCESSING) {
            val pct = GameLibrary.percent(e)
            top.addView(ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
                isIndeterminate = pct < 0; max = 100; progress = pct.coerceAtLeast(0)
            })
            top.addView(Ui.text(this, "${if (pct >= 0) "$pct% · " else ""}${e.stageMsg}", 13f, Ui.DIM))
        }
        val src = when (e.type) {
            SourceType.URI -> "ISO dipilih lewat SAF (izin baca disimpan)" + if (e.localCopy != null) " · dibaca dari salinan internal" else " · dibaca langsung"
            SourceType.PATH -> "ISO dari folder · dibaca langsung"
            SourceType.BUILTIN -> "ISO uji bawaan (homebrew buatan proyek, bukan data game)"
        }
        top.addView(Ui.text(this, "$src · ${GameLibraryFmt.bytes(e.size)}${if (e.fromCache) " · hasil dari cache" else ""}", 13f, Ui.DIM))
        val actions = Ui.hbox(this)
        val running = Sessions.gameId == e.id && Sessions.handle != 0L
        actions.addView(Ui.button(this, if (running) "Resume" else "Play", true) {
            startActivity(Intent(this, PlayActivity::class.java).putExtra("id", e.id))
        }.apply { isEnabled = e.playable || running; alpha = if (e.playable || running) 1f else 0.4f })
        if (running) actions.addView(Ui.button(this, "Stop") { Sessions.stop(); render() })
        if (e.status == LibStatus.PROCESSING) actions.addView(Ui.button(this, "Batal") { GameLibrary.cancel(e) })
        else actions.addView(Ui.button(this, "Proses ulang") { GameLibrary.refresh(e, force = true) })
        actions.addView(Ui.button(this, "Impor kunci XEX") { importKey() })
        if (App.keyFile.exists()) actions.addView(Ui.button(this, "Hapus kunci") {
            App.keyFile.delete(); AppLog.i("Kunci XEX dihapus"); GameLibrary.refreshAll(force = false)
        })
        if (e.type != SourceType.BUILTIN) actions.addView(Ui.button(this, "Hapus dari library") {
            AlertDialog.Builder(this).setTitle("Hapus ${e.name}?")
                .setMessage("File ISO Anda tidak dihapus; hanya entri library, cache analisis, dan salinan internal (jika ada).")
                .setPositiveButton("Hapus") { _, _ -> GameLibrary.remove(e); finish() }.setNegativeButton("Batal", null).show()
        })
        top.addView(actions)
        if (!e.playable && e.status != LibStatus.PROCESSING)
            top.addView(Ui.text(this, "Play hanya aktif untuk status Ready. Aplikasi tidak memakai emulator atau simulasi gameplay sebagai pengganti.", 12f, Ui.DIM))
        body.addView(top)

        val r = e.report
        if (r != null && e.status != LibStatus.PROCESSING) {
            val req = Ui.card(this)
            req.addView(Ui.section(this, "PERSYARATAN RUNTIME (DIPERIKSA OTOMATIS DARI ISO)"))
            r.optJSONArray("requirements")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val q = arr.getJSONObject(i)
                    req.addView(Ui.statusRow(this, q.optString("label"), Ui.Status.of(q.optString("state")), q.optString("detail")))
                }
            }
            if ((r.optJSONArray("requirements")?.length() ?: 0) == 0)
                req.addView(Ui.statusRow(this, "Pemrosesan", Ui.Status.MISSING, r.optString("status_reason")))
            body.addView(req)
            if (r.optBoolean("ok")) body.addView(discCard(r))
            r.optJSONObject("xex")?.let { body.addView(xexCard(it)) }
        }
        body.addView(settingsCard(e))
    }

    private fun discCard(r: JSONObject): View {
        val c = Ui.card(this)
        c.addView(Ui.section(this, "ISI DISC"))
        fun line(k: String, v: String) = c.addView(Ui.text(this, "$k: $v", 13f))
        line("Layout", "${r.optString("layout")} (partisi di offset 0x%X)".format(r.optLong("partition_offset")))
        line("Platform", when (r.optString("platform")) { "xbox360" -> "Xbox 360"; "xbox" -> "Xbox original"; else -> "tidak diketahui" })
        line("Berkas", "${r.optLong("files")} berkas, ${r.optLong("dirs")} folder, ${GameLibraryFmt.bytes(r.optLong("bytes"))} data")
        line("Executable utama", r.optString("main_xex").ifEmpty { "-" })
        line("Lama pemrosesan", "%.2f s".format(r.optDouble("seconds")))
        r.optJSONObject("categories")?.let { cat ->
            val names = mapOf("executable" to "Executable", "title-patch" to "Patch", "video" to "Video", "audio" to "Audio",
                "texture" to "Tekstur/gambar", "ui" to "UI", "archive" to "Arsip data", "config" to "Konfigurasi/teks",
                "script" to "Skrip", "font" to "Font", "shader" to "Shader", "metadata" to "Metadata", "other" to "Lainnya")
            c.addView(Ui.section(this, "KLASIFIKASI FILE"))
            cat.keys().asSequence().sortedByDescending { cat.getJSONObject(it).optLong("bytes") }.forEach { k ->
                val o = cat.getJSONObject(k)
                c.addView(Ui.text(this, "${names[k] ?: k}: ${o.optLong("files")} berkas, ${GameLibraryFmt.bytes(o.optLong("bytes"))}", 13f, Ui.DIM))
            }
        }
        r.optJSONArray("special_dirs")?.takeIf { it.length() > 0 }?.let { a ->
            line("Folder sistem", (0 until a.length()).joinToString { a.getString(it) } + " (tidak dibutuhkan runtime)")
        }
        r.optJSONArray("modules")?.takeIf { it.length() > 0 }?.let { a ->
            c.addView(Ui.section(this, "MODUL XEX LAIN"))
            for (i in 0 until a.length()) {
                val m = a.getJSONObject(i)
                c.addView(Ui.text(this, "${m.optString("path")} · ${GameLibraryFmt.bytes(m.optLong("size"))} · " +
                    (if (m.optBoolean("dll")) "DLL" else if (m.optBoolean("patch")) "patch" else "executable") +
                    " · ${m.optString("load_status").ifEmpty { m.optString("error") }}", 13f, Ui.DIM))
            }
        }
        r.optJSONObject("refs")?.let { refs ->
            if (refs.optLong("total") > 0 || refs.optLong("patterns") > 0) {
                c.addView(Ui.section(this, "FILE YANG DIRUJUK KODE"))
                c.addView(Ui.text(this, "${refs.optLong("found")}/${refs.optLong("total")} ditemukan di ISO · ${refs.optLong("patterns")} pola dinamis · " +
                    "${refs.optLong("other_device")} ke perangkat lain", 13f))
                refs.optJSONArray("missing")?.let { m ->
                    for (i in 0 until minOf(m.length(), 15)) c.addView(Ui.text(this, "✘ ${m.getString(i)}", 12f, Ui.PARTIAL).apply { typeface = Typeface.MONOSPACE })
                }
                refs.optJSONArray("found_examples")?.let { m ->
                    for (i in 0 until minOf(m.length(), 8)) c.addView(Ui.text(this, "✔ ${m.getString(i)}", 12f, Ui.DIM).apply { typeface = Typeface.MONOSPACE })
                }
            }
        }
        r.optJSONArray("truncated_examples")?.takeIf { it.length() > 0 }?.let { a ->
            c.addView(Ui.section(this, "BERKAS TERPOTONG"))
            for (i in 0 until a.length()) c.addView(Ui.text(this, a.getString(i), 12f, Ui.NO))
        }
        return c
    }

    private fun xexCard(a: JSONObject): View {
        val c = Ui.card(this)
        c.addView(Ui.section(this, "EXECUTABLE (DIBACA LANGSUNG DARI ISO)"))
        fun line(k: String, v: String) = c.addView(Ui.text(this, "$k: $v", 13f))
        if (!a.optBoolean("parsed")) { line("Error", a.optString("error")); return c }
        line("Title ID / Media ID", "${a.optString("title_id")} / ${a.optString("media_id")}")
        line("Versi · disc", "${a.optString("version")} · ${a.optInt("disc")}/${a.optInt("discs")}")
        line("Region", a.optString("region_text"))
        line("PE asli", a.optString("pe_name"))
        line("Enkripsi / kompresi", "${if (a.optInt("encryption") == 1) "AES" else "tidak ada"} / " +
            listOf("tidak ada", "basic", "LZX", "delta").getOrElse(a.optInt("compression")) { "?" })
        line("Status image", a.optString("load_status") + a.optString("load_error").let { if (it.isNotEmpty()) " – $it" else "" })
        if (a.has("functions"))
            line("Kode", "%,d fungsi, %,d instruksi, %.2f%% dapat diterjemahkan".format(a.optLong("functions"), a.optLong("instructions"), a.optDouble("coverage") * 100))
        a.optJSONArray("libraries")?.let { line("Library impor", (0 until it.length()).joinToString { i -> it.getString(i) }) }
        a.optJSONArray("imports")?.let { im ->
            c.addView(Ui.section(this, "IMPOR KERNEL (${a.optInt("imports_implemented")}/${im.length()} diimplementasikan)"))
            val sb = StringBuilder()
            for (i in 0 until im.length()) {
                val o = im.getJSONObject(i)
                sb.append(if (o.optBoolean("impl")) "✔ " else "✘ ").append(o.optString("lib")).append('!').append(o.optString("name")).append('\n')
            }
            c.addView(Ui.text(this, sb.toString().trimEnd(), 12f, Ui.DIM).apply { typeface = Typeface.MONOSPACE })
        }
        return c
    }

    private fun settingsCard(e: GameEntry): View {
        val c = Ui.card(this)
        c.addView(Ui.section(this, "PENGATURAN GAME INI (TERSIMPAN OTOMATIS)"))
        val custom = Store.gameGraphics(e.id)
        c.addView(Ui.switchRow(this, "Gunakan pengaturan grafis khusus game", custom != null) { on ->
            Store.saveGameGraphics(e.id, if (on) Store.globalGraphics() else null); render()
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
        return c
    }

    private fun importKey() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply { addCategory(Intent.CATEGORY_OPENABLE); type = "*/*" }, REQ_KEY)
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
        AppLog.i("Kunci XEX diimpor ke penyimpanan privat aplikasi; library diproses ulang")
        GameLibrary.refreshAll(force = false)
    }

    companion object { const val REQ_KEY = 51 }
}
