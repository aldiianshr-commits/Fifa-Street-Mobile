package com.aldiianshr.fifastreet

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.TextView

/** Game library: folder selection, automatic scan, per-title status. */
class MainActivity : Activity() {
    private lateinit var list: ListView
    private lateinit var status: TextView
    private lateinit var permCard: View
    private val adapter = GameAdapter()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = Ui.vbox(this, 0)
        val top = Ui.hbox(this).apply { setPadding(Ui.dpi(this@MainActivity, 12), Ui.dpi(this@MainActivity, 10), Ui.dpi(this@MainActivity, 12), 0) }
        top.addView(ImageView(this).apply {
            setImageResource(R.drawable.app_logo)
            layoutParams = LinearLayout.LayoutParams(Ui.dpi(this@MainActivity, 56), Ui.dpi(this@MainActivity, 56))
        })
        val titles = Ui.vbox(this, 0).apply {
            setPadding(Ui.dpi(this@MainActivity, 12), 0, 0, 0)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        titles.addView(Ui.text(this, "FIFA STREET Recompiled", 22f, Ui.TEXT, true))
        status = Ui.text(this, "", 13f, Ui.DIM)
        titles.addView(status)
        top.addView(titles)
        top.addView(Ui.button(this, "Pilih folder", true) { pickFolder() })
        top.addView(Ui.button(this, "Pindai ulang") { GameLibrary.rescan(force = true) })
        top.addView(Ui.button(this, "Kontrol") { startActivity(Intent(this, ControlsActivity::class.java)) })
        top.addView(Ui.button(this, "Pengaturan") { startActivity(Intent(this, SettingsActivity::class.java)) })
        top.addView(Ui.button(this, "Log") { startActivity(Intent(this, LogActivity::class.java)) })
        root.addView(top)

        permCard = Ui.card(this).apply {
            (layoutParams as LinearLayout.LayoutParams).setMargins(Ui.dpi(this@MainActivity, 12), Ui.dpi(this@MainActivity, 8), Ui.dpi(this@MainActivity, 12), 0)
            addView(Ui.text(this@MainActivity, "Izin \"Akses semua file\" diperlukan agar runtime native dapat membaca folder game Anda (mis. /storage/emulated/0/FifaStreet).", 14f))
            addView(Ui.button(this@MainActivity, "Berikan izin", true) {
                runCatching {
                    startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
                }.onFailure { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) }
            })
        }
        root.addView(permCard)

        list = ListView(this).apply {
            adapter = this@MainActivity.adapter
            divider = null
            setPadding(Ui.dpi(this@MainActivity, 12), Ui.dpi(this@MainActivity, 8), Ui.dpi(this@MainActivity, 12), 0)
            clipToPadding = false
            setOnItemClickListener { _, _, pos, _ ->
                val g = this@MainActivity.adapter.getItem(pos)
                startActivity(Intent(this@MainActivity, GameActivity::class.java).putExtra("id", g.id))
            }
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        root.addView(list)
        setContentView(root)
        GameLibrary.listener = { refresh() }
        if (GameLibrary.games.isEmpty()) GameLibrary.rescan()
        // Headless smoke tests (used by CI on an emulator): adb shell am start ... --es autotest selftest|play
        when (intent.getStringExtra("autotest")) {
            "selftest" -> startActivity(Intent(this, SettingsActivity::class.java).putExtra("autotest", true))
            "play" -> startActivity(Intent(this, PlayActivity::class.java).putExtra("id", "homebrew")
                .putExtra("autotest_seconds", intent.getIntExtra("seconds", 10)))
        }
    }

    override fun onResume() {
        super.onResume()
        GameLibrary.listener = { refresh() }
        permCard.visibility = if (Environment.isExternalStorageManager()) View.GONE else View.VISIBLE
        refresh()
    }

    private fun refresh() {
        val folder = Store.gameFolder ?: "(belum dipilih – default disarankan /storage/emulated/0/FifaStreet)"
        status.text = "Folder: $folder · ${GameLibrary.scanStatus}"
        adapter.notifyDataSetChanged()
    }

    private fun pickFolder() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        runCatching { startActivityForResult(i, REQ_TREE) }.onFailure {
            Ui.prompt(this, "Path folder game", Store.gameFolder ?: "/storage/emulated/0/FifaStreet") { setFolder(it) }
        }
    }

    @Deprecated("framework API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_TREE && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            val path = treeUriToPath(uri)
            if (path == null) {
                Ui.prompt(this, "Lokasi tidak dapat dipetakan ke path. Masukkan path manual:", "/storage/emulated/0/FifaStreet") { setFolder(it) }
            } else setFolder(path)
        }
    }

    private fun setFolder(path: String) {
        Store.gameFolder = path
        AppLog.i("Folder game: $path")
        GameLibrary.rescan(force = false)
        refresh()
    }

    /** Maps an ExternalStorage tree URI to a filesystem path the native runtime can open. */
    private fun treeUriToPath(uri: Uri): String? {
        if (uri.authority != "com.android.externalstorage.documents") return null
        val doc = DocumentsContract.getTreeDocumentId(uri)
        val parts = doc.split(":", limit = 2)
        val vol = parts[0]
        val rel = parts.getOrElse(1) { "" }
        val base = if (vol.equals("primary", true)) Environment.getExternalStorageDirectory().path else "/storage/$vol"
        return if (rel.isEmpty()) base else "$base/$rel"
    }

    inner class GameAdapter : BaseAdapter() {
        private fun items() = synchronized(GameLibrary.games) { GameLibrary.games.toList() }
        override fun getCount() = items().size
        override fun getItem(position: Int) = items()[position]
        override fun getItemId(position: Int) = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val g = getItem(position)
            val ctx = this@MainActivity
            val card = Ui.card(ctx)
            val row = Ui.hbox(ctx)
            val col = Ui.vbox(ctx, 0).apply { layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f) }
            col.addView(Ui.text(ctx, g.name, 17f, Ui.TEXT, true))
            val tid = if (g.titleId.isNotEmpty()) " · Title ID ${g.titleId}" else ""
            col.addView(Ui.text(ctx, "${g.kind.label}$tid", 13f, Ui.DIM))
            col.addView(Ui.text(ctx, g.state, 13f, Ui.TEXT))
            row.addView(col)
            if (g.busy) row.addView(ProgressBar(ctx).apply { layoutParams = LinearLayout.LayoutParams(Ui.dpi(ctx, 28), Ui.dpi(ctx, 28)) })
            val (label, color) = GameLibrary.overall(g)
            row.addView(Ui.badge(ctx, label, color).apply { gravity = Gravity.CENTER })
            card.addView(row)
            return LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, 0, 0, Ui.dpi(ctx, 8))
                addView(card)
            }
        }
    }

    companion object { const val REQ_TREE = 41 }
}
