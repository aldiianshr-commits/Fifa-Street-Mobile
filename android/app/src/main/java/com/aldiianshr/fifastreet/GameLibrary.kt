package com.aldiianshr.fifastreet

import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.StatFs
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/** Library status shown to the user (computed by the native disc scanner). */
enum class LibStatus(val label: String, val color: Int) {
    READY("Ready", Ui.OK), PROCESSING("Processing", Ui.ACCENT), PARTIAL("Partial", Ui.PARTIAL), UNSUPPORTED("Unsupported", Ui.NO)
}

enum class SourceType { URI, PATH, BUILTIN }

/** One disc image in the library. The ISO is the only input; everything else is derived from it. */
class GameEntry(
    val id: String,
    val type: SourceType,
    val location: String,      // content:// URI, absolute path, or asset name
    var name: String,
    var size: Long = -1,
    var mtime: Long = 0,
    val addedAt: Long = System.currentTimeMillis(),
) {
    var localCopy: String? = null          // internal copy when the provider is not seekable
    var report: JSONObject? = null
    var fromCache = false
    @Volatile var processing = false
    @Volatile var queued = false
    @Volatile var stage = ""
    @Volatile var stageMsg = ""
    @Volatile var done = 0L
    @Volatile var total = 0L
    val cancel = AtomicBoolean(false)

    val status: LibStatus
        get() = when {
            processing || queued -> LibStatus.PROCESSING
            report == null -> LibStatus.PROCESSING
            else -> when (report!!.optString("status")) {
                "ready" -> LibStatus.READY
                "partial" -> LibStatus.PARTIAL
                else -> LibStatus.UNSUPPORTED
            }
        }
    val reason: String get() = report?.optString("status_reason").orEmpty()
    val xex: JSONObject? get() = report?.optJSONObject("xex")
    val titleId: String get() = xex?.optString("title_id").orEmpty()
    val playable: Boolean get() = status == LibStatus.READY

    fun toJson(): JSONObject = JSONObject().put("id", id).put("type", type.name).put("location", location).put("name", name)
        .put("size", size).put("mtime", mtime).put("addedAt", addedAt).put("localCopy", localCopy ?: JSONObject.NULL)

    companion object {
        fun fromJson(o: JSONObject) = GameEntry(
            o.getString("id"), SourceType.valueOf(o.getString("type")), o.getString("location"), o.optString("name"),
            o.optLong("size", -1), o.optLong("mtime"), o.optLong("addedAt"),
        ).also { e -> e.localCopy = if (o.isNull("localCopy")) null else o.optString("localCopy", null) }
    }
}

object GameLibrary {
    const val BUILTIN_ID = "homebrew"
    private val worker = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    val games = mutableListOf<GameEntry>()
    var listener: (() -> Unit)? = null
    private var loaded = false

    private val libraryFile get() = File(App.instance.filesDir, "library.json")
    private fun cacheFile(e: GameEntry) = File(App.cacheRoot, "${e.id}.disc.json")
    private val importDir get() = File(App.instance.filesDir, "imported").apply { mkdirs() }

    fun notifyUi() = main.post { listener?.invoke() }

    fun find(id: String) = synchronized(games) { games.firstOrNull { it.id == id } }
    fun snapshot() = synchronized(games) { games.toList() }

    fun idFor(s: String): String =
        MessageDigest.getInstance("SHA-1").digest(s.toByteArray()).joinToString("") { "%02x".format(it) }.take(16)

    // ------------------------------------------------------------ persistence
    @Synchronized
    fun load() {
        if (loaded) return
        loaded = true
        val list = mutableListOf<GameEntry>()
        runCatching {
            if (libraryFile.exists()) {
                val arr = JSONArray(libraryFile.readText())
                for (i in 0 until arr.length()) list += GameEntry.fromJson(arr.getJSONObject(i))
            }
        }.onFailure { AppLog.i("library.json rusak, dibuat ulang: $it") }
        if (list.none { it.id == BUILTIN_ID })
            list.add(0, GameEntry(BUILTIN_ID, SourceType.BUILTIN, App.homebrewIso.path, "Homebrew Self-Test (ISO uji bawaan)"))
        synchronized(games) { games.clear(); games += list }
        AppLog.i("Library dimuat: ${list.size} entri")
        list.forEach { refresh(it, force = false) }
    }

    private fun save() {
        val arr = JSONArray()
        snapshot().forEach { arr.put(it.toJson()) }
        libraryFile.writeText(arr.toString())
    }

    // ------------------------------------------------------------ adding / removing
    /** Adds an ISO picked through the Storage Access Framework (read permission persisted). */
    fun addUri(uri: Uri): GameEntry {
        val cr = App.instance.contentResolver
        runCatching { cr.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            .onFailure { AppLog.i("Izin persisten tidak tersedia untuk $uri: $it") }
        val id = idFor(uri.toString())
        val existing = find(id)
        val meta = queryMeta(uri)
        val e = existing ?: GameEntry(id, SourceType.URI, uri.toString(), meta.first ?: (uri.lastPathSegment ?: "ISO"))
        meta.first?.let { e.name = it }
        synchronized(games) { if (existing == null) games += e }
        save()
        AppLog.i("ISO ditambahkan: ${e.name}" + if (meta.second >= 0) " (${GameLibraryFmt.bytes(meta.second)})" else "")
        refresh(e, force = existing != null)
        return e
    }

    fun addPath(f: File): GameEntry {
        val id = idFor(f.absolutePath)
        val e = find(id) ?: GameEntry(id, SourceType.PATH, f.absolutePath, f.name).also { synchronized(games) { games += it } }
        save()
        refresh(e, force = false)
        return e
    }

    fun remove(e: GameEntry) {
        if (e.type == SourceType.BUILTIN) return
        e.cancel.set(true)
        if (Sessions.gameId == e.id) Sessions.stop()
        if (e.type == SourceType.URI) runCatching {
            App.instance.contentResolver.releasePersistableUriPermission(Uri.parse(e.location), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        e.localCopy?.let { File(it).delete() }
        cacheFile(e).delete()
        synchronized(games) { games.remove(e) }
        save()
        AppLog.i("Dihapus dari library: ${e.name}")
        notifyUi()
    }

    /** Finds ISO files below a folder (requires "All files access"). */
    fun scanFolder(dir: File) {
        worker.execute {
            var n = 0
            fun walk(d: File, depth: Int) {
                if (depth > 5) return
                d.listFiles()?.sortedBy { it.name.lowercase() }?.forEach { f ->
                    if (f.isDirectory && !f.name.startsWith(".")) walk(f, depth + 1)
                    else if (f.isFile && (f.name.endsWith(".iso", true) || f.name.endsWith(".xiso", true)) &&
                        runCatching { NativeBridge.sniff(f.path) }.getOrDefault(0) == NativeBridge.KIND_ISO) {
                        n++
                        main.post { addPath(f) }
                    }
                }
            }
            walk(dir, 0)
            AppLog.i("Pindai folder ${dir.path}: $n ISO Xbox 360 ditemukan")
        }
    }

    // ------------------------------------------------------------ metadata / descriptors
    private fun queryMeta(uri: Uri): Pair<String?, Long> {
        var name: String? = null
        var size = -1L
        runCatching {
            App.instance.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    name = c.getString(0)
                    if (!c.isNull(1)) size = c.getLong(1)
                }
            }
        }
        return name to size
    }

    private fun queryMtime(uri: Uri): Long = runCatching {
        App.instance.contentResolver.query(uri, arrayOf(DocumentsContract.Document.COLUMN_LAST_MODIFIED), null, null, null)?.use { c ->
            if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else 0L
        } ?: 0L
    }.getOrDefault(0L)

    /** Refreshes size/mtime; returns an error text when the source is gone or permission was revoked. */
    private fun stat(e: GameEntry): String? {
        val local = e.localCopy?.let { File(it) }?.takeIf { it.exists() }
        when {
            local != null -> { e.size = local.length(); e.mtime = local.lastModified() }
            e.type == SourceType.URI -> {
                val uri = Uri.parse(e.location)
                val (_, size) = queryMeta(uri)
                if (size < 0 && runCatching { App.instance.contentResolver.openFileDescriptor(uri, "r")?.close() }.isFailure)
                    return "ISO tidak dapat diakses lagi (dipindah, dihapus, atau izin akses dicabut). Pilih ulang file ISO."
                e.size = size
                e.mtime = queryMtime(uri)
            }
            else -> {
                val f = File(e.location)
                if (!f.exists()) return "File tidak ditemukan: ${e.location}"
                e.size = f.length(); e.mtime = f.lastModified()
            }
        }
        return null
    }

    /** Opens the image read-only and returns a detached fd (native takes ownership). */
    fun openFd(e: GameEntry): Int {
        e.localCopy?.let { p -> if (File(p).exists()) return ParcelFileDescriptor.open(File(p), ParcelFileDescriptor.MODE_READ_ONLY).detachFd() }
        return when (e.type) {
            SourceType.URI -> App.instance.contentResolver.openFileDescriptor(Uri.parse(e.location), "r")?.detachFd()
                ?: throw IllegalStateException("penyedia penyimpanan tidak mengembalikan file descriptor")
            else -> ParcelFileDescriptor.open(File(e.location), ParcelFileDescriptor.MODE_READ_ONLY).detachFd()
        }
    }

    private fun stamp(e: GameEntry): String {
        val key = App.keyFile.takeIf { it.exists() }?.let { "${it.length()}@${it.lastModified()}" } ?: "nokey"
        val mods = runCatching { NativeBridge.modulesJson() }.getOrDefault("").hashCode()
        return "${e.size}:${e.mtime}:${e.localCopy != null}:$key:${BuildConfigCompat.versionName()}:$mods"
    }

    // ------------------------------------------------------------ processing
    /** Uses the cached report when the ISO, key and app build are unchanged; otherwise re-processes. */
    fun refresh(e: GameEntry, force: Boolean) {
        e.cancel.set(false)
        e.queued = true
        notifyUi()
        worker.execute { process(e, force) }
    }

    fun cancel(e: GameEntry) { e.cancel.set(true) }

    fun refreshAll(force: Boolean) = snapshot().forEach { refresh(it, force) }

    private fun setStage(e: GameEntry, stage: String, done: Long, total: Long, msg: String) {
        e.stage = stage; e.done = done; e.total = total; e.stageMsg = msg
        notifyUi()
    }

    private fun process(e: GameEntry, force: Boolean) {
        e.queued = false
        if (find(e.id) == null) return
        e.processing = true
        try {
            stat(e)?.let { err ->
                e.report = JSONObject().put("ok", false).put("status", "unsupported").put("status_reason", err).put("error", err)
                AppLog.i("${e.name}: $err")
                return
            }
            val st = stamp(e)
            val cf = cacheFile(e)
            if (!force && cf.exists()) {
                val c = runCatching { JSONObject(cf.readText()) }.getOrNull()
                if (c != null && c.optString("stamp") == st) {
                    e.report = c.optJSONObject("report")
                    e.fromCache = true
                    AppLog.i("${e.name}: hasil analisis dari cache (${e.report?.optString("status")})")
                    return
                }
            }
            e.fromCache = false
            setStage(e, "open", 0, 1, "Membuka ISO")
            val key = App.keyFile.takeIf { it.exists() }?.path
            val cb = object : ScanCallback {
                override fun onProgress(stage: String, done: Long, total: Long, msg: String): Boolean {
                    setStage(e, stage, done, total, msg)
                    return !e.cancel.get()
                }
            }
            var json = JSONObject(NativeBridge.scanDisc(openFd(e), key, true, cb))
            if (json.optBoolean("open_failed") && !json.optBoolean("seekable", true) && e.type == SourceType.URI && e.localCopy == null) {
                // Provider gives a stream only: copy the ISO into app storage once, then read it in place.
                AppLog.i("${e.name}: ${json.optString("error")} → menyalin ke penyimpanan internal")
                val copied = copyToInternal(e)
                if (copied != null) {
                    e.localCopy = copied.path
                    save()
                    e.size = copied.length(); e.mtime = copied.lastModified()
                    json = JSONObject(NativeBridge.scanDisc(openFd(e), key, true, cb))
                } else {
                    json.put("status_reason", json.optString("status_reason") + ". " + lastCopyError)
                }
            }
            if (json.optBoolean("cancelled")) {
                json = JSONObject().put("ok", false).put("status", "unsupported").put("status_reason", "Pemrosesan dibatalkan pengguna")
                    .put("cancelled", true)
            } else {
                cf.writeText(JSONObject().put("stamp", stamp(e)).put("report", json).toString())
            }
            e.report = json
            if (e.size < 0 && json.optLong("image_size", -1) > 0) e.size = json.optLong("image_size")
            AppLog.i("${e.name}: ${json.optString("status")} – ${json.optString("status_reason")} (${"%.2f".format(json.optDouble("seconds", 0.0))} s)")
        } catch (t: Throwable) {
            val msg = "ISO tidak dapat dibuka: ${t.message ?: t}"
            e.report = JSONObject().put("ok", false).put("status", "unsupported").put("status_reason", msg).put("error", msg)
            AppLog.i("${e.name}: $msg")
        } finally {
            e.processing = false
            e.stage = ""
            save()
            notifyUi()
        }
    }

    private var lastCopyError = ""

    private fun copyToInternal(e: GameEntry): File? {
        val uri = Uri.parse(e.location)
        val need = if (e.size > 0) e.size else 0L
        val free = StatFs(importDir.path).availableBytes
        if (need > 0 && free < need + (512L shl 20)) {
            lastCopyError = "Ruang internal tidak cukup untuk salinan (${GameLibraryFmt.bytes(need)} dibutuhkan, ${GameLibraryFmt.bytes(free)} tersedia). " +
                "Simpan ISO di penyimpanan lokal perangkat agar dapat dibaca langsung."
            return null
        }
        val out = File(importDir, "${e.id}.iso")
        val tmp = File(importDir, "${e.id}.iso.part")
        return try {
            App.instance.contentResolver.openInputStream(uri)!!.use { input ->
                tmp.outputStream().use { o ->
                    val buf = ByteArray(1 shl 20)
                    var copied = 0L
                    while (true) {
                        if (e.cancel.get()) throw InterruptedException("dibatalkan")
                        val n = input.read(buf)
                        if (n < 0) break
                        o.write(buf, 0, n)
                        copied += n
                        setStage(e, "copy", copied, need, "Menyalin ISO ke penyimpanan aplikasi")
                    }
                }
            }
            tmp.renameTo(out)
            AppLog.i("${e.name}: salinan internal ${GameLibraryFmt.bytes(out.length())}")
            out
        } catch (t: Throwable) {
            tmp.delete()
            lastCopyError = "Penyalinan gagal: ${t.message}"
            null
        }
    }

    // ------------------------------------------------------------ presentation helpers
    private val stageNames = mapOf(
        "open" to "Membuka ISO", "validate" to "Validasi format", "filesystem" to "Membaca filesystem",
        "inventory" to "Memindai isi ISO", "xex" to "Membaca executable", "decrypt" to "Dekripsi & dekompresi",
        "analyze" to "Analisis & cek terjemahan kode", "modules" to "Memeriksa modul", "references" to "Memeriksa file data",
        "copy" to "Menyalin ke penyimpanan internal", "done" to "Selesai",
    )
    private val stageRange = mapOf(
        "open" to (0 to 1), "validate" to (1 to 2), "filesystem" to (2 to 5), "inventory" to (5 to 30), "xex" to (30 to 34),
        "decrypt" to (34 to 40), "analyze" to (40 to 90), "modules" to (90 to 93), "references" to (93 to 98),
        "done" to (98 to 100), "copy" to (0 to 100),
    )

    fun stageLabel(e: GameEntry): String = when {
        e.queued && !e.processing -> "Dalam antrean"
        else -> stageNames[e.stage] ?: e.stage.ifEmpty { "Menyiapkan" }
    }

    /** Overall percent 0..100, or -1 when indeterminate. */
    fun percent(e: GameEntry): Int {
        val r = stageRange[e.stage] ?: return -1
        val frac = if (e.total > 0) (e.done.toDouble() / e.total).coerceIn(0.0, 1.0) else 0.0
        return (r.first + (r.second - r.first) * frac).toInt()
    }
}

/** JNI progress callback (called synchronously on the worker thread). */
interface ScanCallback {
    fun onProgress(stage: String, done: Long, total: Long, msg: String): Boolean
}

object GameLibraryFmt {
    fun bytes(b: Long): String = when {
        b < 0 -> "?"
        b >= 1L shl 30 -> "%.2f GB".format(b / 1073741824.0)
        b >= 1L shl 20 -> "%.1f MB".format(b / 1048576.0)
        b >= 1L shl 10 -> "%.1f KB".format(b / 1024.0)
        else -> "$b B"
    }
}
