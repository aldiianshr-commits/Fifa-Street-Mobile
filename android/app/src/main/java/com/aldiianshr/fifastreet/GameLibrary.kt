package com.aldiianshr.fifastreet

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.Executors

enum class GameKind(val label: String) {
    HOMEBREW("Homebrew bawaan"), XEX("Folder game (XEX)"), ISO("Image disc (ISO/XDVDFS)"), STFS("Paket STFS (GOD/LIVE/CON)")
}

class GameEntry(
    val id: String,
    val kind: GameKind,
    val path: String,
    var name: String,
) {
    var xexPath: String? = null          // XEX actually analysed / run
    var state = "Menunggu analisis"
    var analysis: JSONObject? = null
    var containerInfo: JSONObject? = null // ISO listing summary / folder stats
    var busy = false

    val titleId: String get() = analysis?.optString("title_id") ?: ""
    val parsed: Boolean get() = analysis?.optBoolean("parsed") == true
    val loadStatus: String get() = analysis?.optString("load_status") ?: ""
    val imageLoaded: Boolean get() = loadStatus == "ok"
    val module: String? get() = analysis?.let { if (it.isNull("module")) null else it.optString("module", null) }
    val playable: Boolean get() = module != null
}

data class Feature(val label: String, val status: Ui.Status, val detail: String)

object GameLibrary {
    private val exec = Executors.newSingleThreadExecutor()
    private val main = Handler(Looper.getMainLooper())
    val games = mutableListOf<GameEntry>()
    var listener: (() -> Unit)? = null
    @Volatile var scanning = false
    var scanStatus = ""

    fun idFor(path: String): String =
        MessageDigest.getInstance("SHA-1").digest(path.toByteArray()).joinToString("") { "%02x".format(it) }.take(16)

    private fun notifyUi() = main.post { listener?.invoke() }

    /** Rescan the selected folder (plus the built-in homebrew entry). */
    fun rescan(force: Boolean = false) {
        if (scanning) return
        scanning = true
        scanStatus = "Memindai…"
        notifyUi()
        exec.execute {
            val found = mutableListOf<GameEntry>()
            found += GameEntry("homebrew", GameKind.HOMEBREW, App.homebrewXex.path, "Homebrew Self-Test (uji runtime)").also {
                it.xexPath = App.homebrewXex.path
            }
            val root = Store.gameFolder?.let { File(it) }
            var visited = 0
            if (root != null && root.isDirectory) {
                walk(root, 0, found) { visited++; if (visited % 50 == 0) { scanStatus = "Memindai… $visited berkas"; notifyUi() } }
            } else if (root != null) {
                AppLog.i("Folder game tidak dapat dibaca: ${root.path} (izin 'Akses semua file' sudah diberikan?)")
            }
            synchronized(games) { games.clear(); games += found }
            scanStatus = "Ditemukan ${found.size - 1} kandidat game"
            AppLog.i("Scan: $scanStatus di ${root?.path ?: "(belum memilih folder)"}")
            notifyUi()
            found.forEach { analyzeBlocking(it, force) }
            scanning = false
            scanStatus = "Selesai: ${found.size - 1} kandidat game"
            notifyUi()
        }
    }

    private fun walk(dir: File, depth: Int, out: MutableList<GameEntry>, tick: () -> Unit) {
        if (depth > 5) return
        val kids = dir.listFiles() ?: return
        for (f in kids.sortedBy { it.name.lowercase() }) {
            if (f.name.startsWith(".")) continue
            tick()
            if (f.isDirectory) { walk(f, depth + 1, out, tick); continue }
            val lower = f.name.lowercase()
            val kind = when {
                lower.endsWith(".xex") -> GameKind.XEX
                lower.endsWith(".iso") || lower.endsWith(".xiso") -> GameKind.ISO
                !lower.contains('.') && f.length() > 0xB000 -> GameKind.STFS
                else -> null
            } ?: continue
            val sniffed = try { NativeBridge.sniff(f.path) } catch (_: Throwable) { 0 }
            val ok = when (kind) {
                GameKind.XEX -> sniffed == NativeBridge.KIND_XEX
                GameKind.ISO -> sniffed == NativeBridge.KIND_ISO
                GameKind.STFS -> sniffed == NativeBridge.KIND_STFS
                else -> false
            }
            if (!ok) continue
            val name = when (kind) {
                GameKind.XEX -> "${f.parentFile?.name ?: ""} / ${f.name}"
                else -> f.name
            }
            out += GameEntry(idFor(f.path), kind, f.path, name)
        }
    }

    fun analyzeAsync(e: GameEntry, force: Boolean = true) = exec.execute { analyzeBlocking(e, force) }

    private fun cacheFile(e: GameEntry) = File(App.cacheRoot, "${e.id}.json")

    private fun analyzeBlocking(e: GameEntry, force: Boolean) {
        val src = File(e.path)
        val key = if (App.keyFile.exists()) App.keyFile.path else null
        val stamp = "${src.length()}:${src.lastModified()}:${key?.let { File(it).lastModified() } ?: 0}:${BuildConfigCompat.versionName()}"
        val cf = cacheFile(e)
        if (!force && cf.exists()) {
            runCatching {
                val c = JSONObject(cf.readText())
                if (c.optString("stamp") == stamp) {
                    e.analysis = c.optJSONObject("analysis"); e.containerInfo = c.optJSONObject("container")
                    e.xexPath = c.optString("xex", null); e.name = c.optString("name", e.name)
                    e.state = stateText(e); notifyUi(); return
                }
            }
        }
        e.busy = true
        try {
            when (e.kind) {
                GameKind.HOMEBREW, GameKind.XEX -> {
                    e.xexPath = e.path
                    if (e.kind == GameKind.XEX) e.containerInfo = folderStats(src.parentFile)
                }
                GameKind.ISO -> {
                    e.state = "Membaca daftar berkas ISO…"; notifyUi()
                    val list = JSONObject(NativeBridge.isoListJson(e.path))
                    e.containerInfo = JSONObject().put("type", "iso").put("ok", list.optBoolean("ok"))
                        .put("files", list.optInt("files")).put("bytes", list.optLong("bytes"))
                        .put("truncated", list.optInt("truncated")).put("error", list.optString("error"))
                    if (list.optBoolean("ok")) {
                        val out = File(App.cacheRoot, "${e.id}_default.xex")
                        e.state = "Mengekstrak default.xex dari ISO…"; notifyUi()
                        e.xexPath = if (NativeBridge.isoExtract(e.path, "default.xex", out.path)) out.path else null
                    }
                }
                GameKind.STFS -> {
                    e.containerInfo = JSONObject().put("type", "stfs")
                    e.xexPath = null
                }
            }
            val xp = e.xexPath
            if (xp != null) {
                e.state = "Menganalisis kode PowerPC…"; notifyUi()
                val t0 = System.currentTimeMillis()
                e.analysis = JSONObject(NativeBridge.analyzeXex(xp, key, true))
                AppLog.i("Analisis ${e.name}: ${System.currentTimeMillis() - t0} ms, title ${e.titleId}, status ${e.loadStatus}")
            }
            cf.writeText(JSONObject().put("stamp", stamp).put("analysis", e.analysis).put("container", e.containerInfo)
                .put("xex", e.xexPath ?: JSONObject.NULL).put("name", e.name).toString())
        } catch (t: Throwable) {
            AppLog.i("Analisis gagal untuk ${e.path}: $t")
            e.analysis = JSONObject().put("parsed", false).put("error", t.toString())
        } finally {
            e.busy = false
            e.state = stateText(e)
            notifyUi()
        }
    }

    private fun folderStats(dir: File?): JSONObject {
        var files = 0
        var bytes = 0L
        fun rec(d: File, depth: Int) {
            if (depth > 8) return
            d.listFiles()?.forEach { if (it.isDirectory) rec(it, depth + 1) else { files++; bytes += it.length() } }
        }
        if (dir != null) rec(dir, 0)
        return JSONObject().put("type", "folder").put("files", files).put("bytes", bytes)
    }

    fun stateText(e: GameEntry): String {
        val a = e.analysis
        return when {
            e.kind == GameKind.STFS -> "Format STFS belum didukung"
            e.xexPath == null && e.kind == GameKind.ISO -> "default.xex tidak ditemukan di ISO"
            a == null -> "Menunggu analisis"
            !a.optBoolean("parsed") -> "Bukan XEX valid: ${a.optString("error")}"
            e.loadStatus == "needs-key" -> "Terenkripsi – perlu kunci XEX milik Anda"
            e.loadStatus == "key-mismatch" -> "Kunci XEX tidak cocok"
            e.loadStatus != "ok" -> "Gagal memuat image: ${a.optString("load_error")}"
            e.playable -> "Dapat dijalankan (modul ${e.module})"
            else -> "Teranalisis – belum ada modul recompile"
        }
    }

    /** Honest per-feature status for the detail screen. */
    fun features(e: GameEntry): List<Feature> {
        val a = e.analysis
        val out = mutableListOf<Feature>()
        val ci = e.containerInfo
        out += when (e.kind) {
            GameKind.STFS -> Feature("Format paket", Ui.Status.MISSING, "Paket STFS (Games on Demand/LIVE/CON) belum dapat dibaca. Gunakan dump ISO atau folder hasil ekstrak.")
            GameKind.ISO -> if (ci?.optBoolean("ok") == true) Feature("Image disc XDVDFS", if (ci.optInt("truncated") == 0) Ui.Status.WORKS else Ui.Status.PARTIAL,
                "${ci.optInt("files")} berkas, ${fmtBytes(ci.optLong("bytes"))}" + if (ci.optInt("truncated") > 0) " – ${ci.optInt("truncated")} berkas terpotong (ISO tidak lengkap)" else " – semua berkas dalam batas file")
            else Feature("Image disc XDVDFS", Ui.Status.MISSING, ci?.optString("error") ?: "")
            GameKind.XEX -> Feature("Folder game", Ui.Status.WORKS, "${ci?.optInt("files") ?: 0} berkas, ${fmtBytes(ci?.optLong("bytes") ?: 0)} di folder executable")
            GameKind.HOMEBREW -> Feature("Program uji", Ui.Status.WORKS, "Homebrew buatan proyek ini (bukan data game berhak cipta)")
        }
        if (a == null) return out
        out += if (a.optBoolean("parsed")) Feature("Header XEX2", Ui.Status.WORKS,
            "Title ID ${a.optString("title_id")}, media ${a.optString("media_id")}, versi ${a.optString("version")}, disc ${a.optInt("disc")}/${a.optInt("discs")}")
        else Feature("Header XEX2", Ui.Status.MISSING, a.optString("error"))
        if (!a.optBoolean("parsed")) return out
        val enc = if (a.optInt("encryption") == 1) "AES" else "tanpa enkripsi"
        val cmp = when (a.optInt("compression")) { 0 -> "tanpa kompresi"; 1 -> "kompresi basic"; 2 -> "kompresi LZX"; else -> "delta patch" }
        out += when (e.loadStatus) {
            "ok" -> Feature("Dekripsi & dekompresi image", Ui.Status.WORKS, "$enc, $cmp → PE ${fmtBytes(a.optLong("image_size"))} di ${a.optString("base")}")
            "needs-key" -> Feature("Dekripsi image", Ui.Status.NEEDS, "$enc, $cmp. Aplikasi tidak menyertakan kunci. Impor kunci XEX dari konsol Anda sendiri, atau pakai XEX yang sudah Anda dekripsi.")
            "key-mismatch" -> Feature("Dekripsi image", Ui.Status.NEEDS, "Kunci yang diimpor tidak cocok (hash blok/MZ tidak valid).")
            else -> Feature("Memuat image", Ui.Status.MISSING, a.optString("load_error"))
        }
        if (e.loadStatus != "ok") return out
        val ins = a.optLong("instructions")
        val tr = a.optLong("translated")
        val vmx = a.optLong("vmx")
        val vmxOk = a.optLong("vmx_translated")
        val cov = a.optDouble("coverage", 0.0) * 100
        val untr = a.optJSONObject("untranslated")
        val top = untr?.keys()?.asSequence()?.map { it to untr.optLong(it) }?.sortedByDescending { it.second }?.take(4)
            ?.joinToString { "${it.first} ×${it.second}" }.orEmpty()
        out += Feature("Analisis & terjemahan kode PowerPC", if (tr == ins) Ui.Status.WORKS else Ui.Status.PARTIAL,
            "%,d fungsi, %,d instruksi; %.2f%% dapat diterjemahkan ke C++/ARM64. VMX/VMX128: %,d dari %,d diterjemahkan (%,d encoding VMX128). Kata tak dikenal: %,d.%s"
                .format(a.optLong("functions"), ins, cov, vmxOk, vmx, a.optLong("vmx128"), a.optLong("invalid"),
                    if (top.isNotEmpty()) " Belum: $top" else ""))
        val impT = a.optInt("imports_total")
        val impI = a.optInt("imports_implemented")
        out += Feature("Impor kernel/XAM (HLE)", if (impT > 0 && impI == impT) Ui.Status.WORKS else if (impI > 0) Ui.Status.PARTIAL else Ui.Status.MISSING,
            "$impI dari $impT impor sudah diimplementasikan runtime")
        out += if (e.playable) Feature("Modul hasil recompile", Ui.Status.WORKS, "Modul '${e.module}' terpasang di APK dan cocok dengan SHA-1 image")
        else Feature("Modul hasil recompile", Ui.Status.MISSING,
            "Belum ada kode hasil recompile untuk executable ini di dalam APK. Jalankan fsr-recomp di PC pada XEX Anda lalu build APK (lihat docs/USAGE.md).")
        val isHb = e.kind == GameKind.HOMEBREW
        out += Feature("Grafis Xenos (GPU Xbox 360) → Vulkan", if (isHb) Ui.Status.PARTIAL else Ui.Status.MISSING,
            if (isHb) "Homebrew menulis framebuffer; Vulkan menampilkan hasilnya. Penerjemah perintah GPU Xenos belum dibuat." else "Belum dibuat: game Xbox 360 butuh terjemahan perintah GPU Xenos & shader.")
        out += Feature("Audio (XAudio2/XMA)", Ui.Status.MISSING, "Belum dibuat. Jalur AAudio perangkat dapat diuji dari Pengaturan.")
        out += Feature("Thread guest & sinkronisasi", if (isHb) Ui.Status.PARTIAL else Ui.Status.MISSING,
            "Guest dijalankan di satu thread host; ExCreateThread/KeWait* belum diimplementasikan.")
        out += Feature("Input (XamInputGetState)", Ui.Status.WORKS, "Kontrol sentuh & gamepad diteruskan ke runtime sebagai state XInput")
        return out
    }

    fun overall(e: GameEntry): Pair<String, Int> = when {
        e.playable -> "Dapat dijalankan" to Ui.OK
        e.imageLoaded -> "Belum dapat dijalankan" to Ui.NO
        e.loadStatus == "needs-key" || e.loadStatus == "key-mismatch" -> "Perlu kunci" to Ui.PARTIAL
        else -> "Tidak didukung" to Ui.NO
    }

    fun fmtBytes(b: Long): String = when {
        b >= 1L shl 30 -> "%.2f GB".format(b / 1073741824.0)
        b >= 1L shl 20 -> "%.1f MB".format(b / 1048576.0)
        b >= 1L shl 10 -> "%.1f KB".format(b / 1024.0)
        else -> "$b B"
    }
}
