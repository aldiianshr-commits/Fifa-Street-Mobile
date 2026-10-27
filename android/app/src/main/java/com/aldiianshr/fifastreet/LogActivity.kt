package com.aldiianshr.fifastreet

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.widget.TextView

/** Runtime log + saved error reports. */
class LogActivity : Activity() {
    private lateinit var view: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val body = Ui.vbox(this, 14)
        body.addView(Ui.header(this, "Log runtime",
            "Segarkan" to { refresh() },
            "Bagikan" to { share(AppLog.snapshot().joinToString("\n")) },
            "Simpan laporan" to { AppLog.report("manual", "Laporan dibuat pengguna"); refresh() },
            "Bersihkan" to { AppLog.clear(); refresh() },
            "Kembali" to { finish() }))
        val reports = Ui.card(this)
        reports.addView(Ui.section(this, "LAPORAN ERROR TERSIMPAN"))
        val files = App.reportsDir.listFiles()?.sortedByDescending { it.lastModified() }.orEmpty()
        if (files.isEmpty()) reports.addView(Ui.text(this, "Belum ada laporan.", 13f, Ui.DIM))
        files.take(20).forEach { f ->
            reports.addView(Ui.button(this, "${f.name}  (bagikan)") { share(f.readText()) })
        }
        body.addView(reports)
        view = Ui.text(this, "", 12f).apply { typeface = Typeface.MONOSPACE; setTextIsSelectable(true) }
        body.addView(view)
        setContentView(Ui.scroll(this, body))
        refresh()
    }

    private fun refresh() {
        view.text = AppLog.snapshot().takeLast(800).joinToString("\n").ifEmpty { "(log kosong)" }
    }

    private fun share(text: String) {
        startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_TEXT, text.takeLast(90_000))
        }, "Bagikan log"))
    }
}
