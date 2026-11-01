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

/** Library: one ISO per game, picked through the Storage Access Framework and processed automatically. */
class MainActivity : Activity() {
    private lateinit var list: ListView
    private lateinit var status: TextView
    private lateinit var empty: View
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
        top.addView(Ui.button(this, "＋ Tambah ISO", true) { pickIso() })
        top.addView(Ui.button(this, "Pindai folder") { pickFolder() })
        top.addView(Ui.button(this, "Kontrol") { startActivity(Intent(this, ControlsActivity::class.java)) })
        top.addView(Ui.button(this, "Pengaturan") { startActivity(Intent(this, SettingsActivity::class.java)) })
        top.addView(Ui.button(this, "Log") { startActivity(Intent(this, LogActivity::class.java)) })
        root.addView(top)

        empty = Ui.card(this).apply {
            (layoutParams as LinearLayout.LayoutParams).setMargins(Ui.dpi(this@MainActivity, 12), Ui.dpi(this@MainActivity, 8), Ui.dpi(this@MainActivity, 12), 0)
            addView(Ui.text(this@MainActivity, "Pilih satu file ISO Xbox 360 milik Anda. Aplikasi membaca ISO langsung (tanpa ekstraksi), memindai seluruh isinya, " +
                "menemukan executable dan file data, lalu menganalisis kode secara otomatis. Tidak perlu memilih XEX atau file internal lain.", 14f))
        }
        root.addView(empty)

        list = ListView(this).apply {
            adapter = this@MainActivity.adapter
            divider = null
            setPadding(Ui.dpi(this@MainActivity, 12), Ui.dpi(this@MainActivity, 8), Ui.dpi(this@MainActivity, 12), 0)
            clipToPadding = false
            setOnItemClickListener { _, _, pos, _ ->
                startActivity(Intent(this@MainActivity, GameActivity::class.java).putExtra("id", this@MainActivity.adapter.getItem(pos).id))
            }
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
        }
        root.addView(list)
        setContentView(root)
        GameLibrary.listener = { refresh() }
        GameLibrary.load()
        handleViewIntent(intent)
        // Headless smoke tests (CI emulator): adb shell am start ... --es autotest selftest|play
        when (intent.getStringExtra("autotest")) {
            "selftest" -> startActivity(Intent(this, SettingsActivity::class.java).putExtra("autotest", true))
            "pickiso" -> pickIso(initialDownloads = true)
            "play" -> startActivity(Intent(this, PlayActivity::class.java).putExtra("id", GameLibrary.BUILTIN_ID)
                .putExtra("autotest_seconds", intent.getIntExtra("seconds", 10)))
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleViewIntent(intent)
    }

    /** "Open with" from a file manager also adds the ISO. */
    private fun handleViewIntent(i: Intent?) {
        if (i?.action == Intent.ACTION_VIEW) i.data?.let { GameLibrary.addUri(it) }
    }

    override fun onResume() {
        super.onResume()
        GameLibrary.listener = { refresh() }
        refresh()
    }

    private fun refresh() {
        val g = GameLibrary.snapshot()
        val c = LibStatus.values().associateWith { s -> g.count { it.status == s } }
        status.text = "${g.size} game · Ready ${c[LibStatus.READY]} · Processing ${c[LibStatus.PROCESSING]} · " +
            "Partial ${c[LibStatus.PARTIAL]} · Unsupported ${c[LibStatus.UNSUPPORTED]}"
        empty.visibility = if (g.count { it.type != SourceType.BUILTIN } == 0) View.VISIBLE else View.GONE
        adapter.notifyDataSetChanged()
    }

    private fun pickIso(initialDownloads: Boolean = false) {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            if (initialDownloads) putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload"))
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/x-iso9660-image", "application/octet-stream", "application/x-cd-image", "*/*"))
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(i, REQ_ISO)
    }

    private fun pickFolder() {
        if (!Environment.isExternalStorageManager()) {
            Ui.info(this, "Izin diperlukan", "Pemindaian folder membaca file lewat path dan membutuhkan izin \"Akses semua file\". " +
                "Alternatifnya, gunakan \"Tambah ISO\" yang tidak membutuhkan izin ini.")
            runCatching { startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))) }
            return
        }
        runCatching { startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_TREE) }.onFailure {
            Ui.prompt(this, "Path folder", Store.gameFolder ?: "/storage/emulated/0/FifaStreet") { scanFolderPath(it) }
        }
    }

    private fun scanFolderPath(path: String) {
        Store.gameFolder = path
        GameLibrary.scanFolder(java.io.File(path))
    }

    @Deprecated("framework API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        when (requestCode) {
            REQ_ISO -> {
                val e = GameLibrary.addUri(uri)
                AppLog.i("AUTOTEST picked uri=$uri persisted=${contentResolver.persistedUriPermissions.any { it.uri == uri && it.isReadPermission }}")
                startActivity(Intent(this, GameActivity::class.java).putExtra("id", e.id))
            }
            REQ_TREE -> treeUriToPath(uri)?.let { scanFolderPath(it) }
                ?: Ui.prompt(this, "Lokasi tidak dapat dipetakan ke path. Masukkan path manual:", "/storage/emulated/0/FifaStreet") { scanFolderPath(it) }
        }
    }

    private fun treeUriToPath(uri: Uri): String? {
        if (uri.authority != "com.android.externalstorage.documents") return null
        val parts = DocumentsContract.getTreeDocumentId(uri).split(":", limit = 2)
        val base = if (parts[0].equals("primary", true)) Environment.getExternalStorageDirectory().path else "/storage/${parts[0]}"
        val rel = parts.getOrElse(1) { "" }
        return if (rel.isEmpty()) base else "$base/$rel"
    }

    inner class GameAdapter : BaseAdapter() {
        override fun getCount() = GameLibrary.snapshot().size
        override fun getItem(position: Int) = GameLibrary.snapshot()[position]
        override fun getItemId(position: Int) = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val g = getItem(position)
            val ctx = this@MainActivity
            val card = Ui.card(ctx)
            val row = Ui.hbox(ctx)
            val col = Ui.vbox(ctx, 0).apply { layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f) }
            col.addView(Ui.text(ctx, g.name, 17f, Ui.TEXT, true))
            val src = when (g.type) { SourceType.URI -> "ISO (SAF)"; SourceType.PATH -> "ISO (folder)"; SourceType.BUILTIN -> "ISO uji bawaan" }
            val tid = if (g.titleId.isNotEmpty()) " · Title ID ${g.titleId}" else ""
            val layout = g.report?.optString("layout")?.takeIf { it.isNotEmpty() }?.let { " · $it" } ?: ""
            col.addView(Ui.text(ctx, "$src · ${GameLibraryFmt.bytes(g.size)}$layout$tid${if (g.fromCache) " · cache" else ""}", 13f, Ui.DIM))
            if (g.status == LibStatus.PROCESSING) {
                val pct = GameLibrary.percent(g)
                col.addView(Ui.text(ctx, "${GameLibrary.stageLabel(g)}${if (pct >= 0) " · $pct%" else ""}  ${g.stageMsg.take(80)}", 13f, Ui.ACCENT))
                col.addView(ProgressBar(ctx, null, android.R.attr.progressBarStyleHorizontal).apply {
                    isIndeterminate = pct < 0
                    max = 100
                    progress = pct.coerceAtLeast(0)
                })
            } else {
                col.addView(Ui.text(ctx, g.reason.take(220), 13f, Ui.TEXT))
            }
            row.addView(col)
            row.addView(Ui.badge(ctx, g.status.label, g.status.color).apply { gravity = Gravity.CENTER })
            card.addView(row)
            return LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, 0, 0, Ui.dpi(ctx, 8))
                addView(card)
            }
        }
    }

    companion object {
        const val REQ_ISO = 40
        const val REQ_TREE = 41
    }
}
