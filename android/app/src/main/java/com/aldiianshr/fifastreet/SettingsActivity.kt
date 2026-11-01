package com.aldiianshr.fifastreet

import android.app.Activity
import android.os.Bundle
import android.view.View
import android.widget.LinearLayout
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.Executors

/** Graphics editor shared by the global settings and per-game settings. */
object GraphicsEditor {
    fun build(act: Activity, g: GraphicsSettings, save: (GraphicsSettings) -> Unit): View {
        val box = Ui.vbox(act, 0)
        fun rebuild() { box.removeAllViews(); box.addView(build(act, g, save)) }
        val c = Ui.vbox(act, 0)
        c.addView(Ui.section(act, "GRAFIS DASAR"))
        val profiles = listOf("Performance", "Balanced", "Quality")
        c.addView(Ui.spinnerRow(act, "Profil", profiles, profiles.indexOf(g.profile).coerceAtLeast(0), { it }) {
            if (it != g.profile) { g.applyProfile(it); save(g); rebuild() }
        })
        val res = listOf(480, 720, 1080)
        c.addView(Ui.spinnerRow(act, "Resolusi internal", res, res.indexOf(g.resolution).coerceAtLeast(1), { "${it}p (${GraphicsSettings(resolution = it).width}×${GraphicsSettings(resolution = it).height})" }) {
            g.resolution = it; save(g)
        })
        val fps = listOf(30, 60)
        c.addView(Ui.spinnerRow(act, "Batas FPS", fps, fps.indexOf(g.fps).coerceAtLeast(1), { "$it FPS" }) { g.fps = it; save(g) })
        c.addView(Ui.switchRow(act, "VSync (FIFO). Mati = MAILBOX/IMMEDIATE bila didukung", g.vsync) { g.vsync = it; save(g) })
        val asp = listOf(0, 1)
        c.addView(Ui.spinnerRow(act, "Aspect ratio", asp, g.aspect, { if (it == 0) "16:9 (letterbox)" else "Regangkan ke layar" }) { g.aspect = it; save(g) })
        c.addView(Ui.switchRow(act, "Texture filtering gambar akhir: Linear (mati = Nearest)", g.linear) { g.linear = it; save(g) })
        c.addView(Ui.switchRow(act, "Anisotropic filtering", false, enabled = false) {})
        c.addView(Ui.text(act, "   Belum tersedia: butuh renderer 3D Xenos (tidak ada tekstur 3D yang disampel saat ini).", 12f, Ui.DIM))

        c.addView(Ui.section(act, "GRAFIS LANJUTAN"))
        c.addView(Ui.seekRow(act, "Render scale", 50, 100, g.scalePct, { "$it%" }) { g.scalePct = it; save(g) })
        c.addView(Ui.switchRow(act, "Adaptif termal: turunkan ke 30 FPS saat perangkat throttling", g.thermalAdapt) { g.thermalAdapt = it; save(g) })
        c.addView(Ui.switchRow(act, "Overlay debug (FPS, waktu frame, resolusi, GPU)", g.showStats) { g.showStats = it; save(g) })
        c.addView(Ui.switchRow(act, "Anti-aliasing (MSAA/FXAA)", false, enabled = false) {})
        c.addView(Ui.switchRow(act, "Shader cache", false, enabled = false) {})
        c.addView(Ui.switchRow(act, "Pengelolaan tekstur / efek grafis game", false, enabled = false) {})
        c.addView(Ui.text(act, "   Tiga opsi di atas belum tersedia: memerlukan penerjemah GPU Xenos → Vulkan (shader & tekstur game). " +
            "Presenter Vulkan saat ini tidak memakai shader.", 12f, Ui.DIM))
        box.addView(c)
        return box
    }
}

class SettingsActivity : Activity() {
    private val bg = Executors.newSingleThreadExecutor()
    private var toneOn = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val body = Ui.vbox(this, 14)
        body.addView(Ui.header(this, "Pengaturan", "Kembali" to { finish() }))

        val g = Store.globalGraphics()
        val gc = Ui.card(this)
        gc.addView(GraphicsEditor.build(this, g) { Store.saveGlobalGraphics(it) })
        gc.addView(Ui.button(this, "Kembalikan default perangkat") {
            Store.saveGlobalGraphics(GraphicsSettings.deviceDefault(this)); recreate()
        })
        body.addView(gc)

        val dev = Ui.card(this)
        dev.addView(Ui.section(this, "PERANGKAT (TERDETEKSI OTOMATIS)"))
        dev.addView(Ui.text(this, DeviceInfo.summary(this), 13f))
        val target = DeviceInfo.isXiaomiPad6() || DeviceInfo.isSnapdragon870()
        dev.addView(Ui.text(this, if (target) "Target utama terdeteksi (Xiaomi Pad 6 / Snapdragon 870)." else "Bukan perangkat target; aplikasi tetap berjalan di ARM64 dengan Vulkan.", 13f, if (target) Ui.OK else Ui.PARTIAL))
        val gpu = Ui.text(this, "GPU Vulkan: memeriksa…", 13f)
        dev.addView(gpu)
        body.addView(dev)
        bg.execute {
            val j = JSONObject(NativeBridge.gpuInfo())
            val s = if (j.optBoolean("ok")) "GPU Vulkan: ${j.optString("name")}, API ${j.optString("api")}, driver ${j.optLong("driver")}, blit linear ${if (j.optBoolean("linear_blit")) "ya" else "tidak"}"
            else "GPU Vulkan: ${j.optString("error")}"
            runOnUiThread { gpu.text = s }
        }

        val rt = Ui.card(this)
        rt.addView(Ui.section(this, "RUNTIME & UJI"))
        val mods = JSONArray(NativeBridge.modulesJson())
        for (i in 0 until mods.length()) {
            val m = mods.getJSONObject(i)
            rt.addView(Ui.text(this, "Modul recompile di APK: ${m.optString("name")} – ${m.optInt("functions")} fungsi, image ${m.optString("base")}, SHA-1 ${m.optString("sha1").take(12)}…", 13f))
        }
        val hle = JSONArray(NativeBridge.hleJson())
        rt.addView(Ui.text(this, "Fungsi kernel/XAM yang diimplementasikan (${hle.length()}): " + (0 until hle.length()).joinToString { hle.getString(it).substringAfter('!') }, 12f, Ui.DIM))
        val result = Ui.text(this, "", 13f)
        val row = Ui.hbox(this)
        row.addView(Ui.button(this, "Uji runtime (integer, FPU, AltiVec, VMX128)", true) {
            result.text = "Menjalankan…"
            bg.execute {
                val j = JSONObject(NativeBridge.selfTest(App.homebrewXex.path))
                val txt = if (!j.optBoolean("ok")) "Gagal: ${j.optString("error")}" else {
                    val res = j.getJSONArray("results")
                    buildString {
                        append("Hasil identik: ${j.optInt("matched")}/${j.optInt("total")}  (hash native ${j.optString("hash_native")}, recompile ${j.optString("hash_recomp")})\n")
                        append("VMX128 (encoding manual): ${if (j.optInt("vmx128_failures") == 0) "semua cek lulus" else "${j.optInt("vmx128_failures")} cek gagal"}\n")
                        append("Waktu per iterasi (diukur di perangkat ini, ${j.optInt("iterations")}×): native ARM64 %.1f µs, PowerPC→ARM64 recompile %.1f µs\n"
                            .format(j.optDouble("native_us"), j.optDouble("recomp_us")))
                        for (i in 0 until res.length()) {
                            val o = res.getJSONObject(i)
                            if (!o.optBoolean("match")) append("  #$i berbeda: ${o.optString("native")} vs ${o.optString("recomp")}\n")
                        }
                    }
                }
                AppLog.i("Self-test: ${txt.lineSequence().first()}")
                runOnUiThread { result.text = txt }
            }
        })
        row.addView(Ui.button(this, "Tes audio (AAudio)") {
            toneOn = !toneOn
            val ok = NativeBridge.audioTestTone(toneOn)
            result.text = if (!ok) "AAudio gagal dibuka (lihat Log)." else if (toneOn) "Nada 440 Hz diputar. Tekan lagi untuk berhenti." else "Audio dihentikan."
        })
        rt.addView(row)
        val row2 = Ui.hbox(this)
        row2.addView(Ui.button(this, "Uji ISO: baca file game:\\ dari ISO") {
            result.text = "Menjalankan…"
            bg.execute {
                val j = JSONObject(NativeBridge.discFileTest(android.os.ParcelFileDescriptor.open(App.homebrewIso,
                    android.os.ParcelFileDescriptor.MODE_READ_ONLY).detachFd()))
                val txt = if (j.optBoolean("ok")) "File I/O guest dari ISO: ${j.optInt("passed")}/${j.optInt("total")} cek lulus, ${j.optLong("bytes")} byte dibaca langsung (pread)"
                else "Gagal: ${j.optString("error")}"
                AppLog.i(txt)
                runOnUiThread { result.text = txt }
            }
        })
        row2.addView(Ui.button(this, "Ekspor ISO uji (untuk mencoba Tambah ISO)") {
            startActivityForResult(android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE)
                type = "application/octet-stream"
                putExtra(android.content.Intent.EXTRA_TITLE, "fsr-homebrew-test.iso")
            }, REQ_EXPORT)
        })
        rt.addView(row2)
        rt.addView(result)
        rt.addView(Ui.text(this, "Audio game (XAudio2/XMA) belum tersedia; tombol di atas hanya menguji jalur audio perangkat.", 12f, Ui.DIM))
        body.addView(rt)

        val about = Ui.card(this)
        about.addView(Ui.section(this, "TENTANG"))
        about.addView(Ui.text(this, "FIFA STREET Recompiled ${BuildConfigCompat.versionName()} – proyek komunitas, tidak berafiliasi dengan EA, FIFA, atau Microsoft. " +
            "Tidak ada file game, firmware, atau kunci dekripsi yang disertakan. Gunakan hanya salinan game yang Anda miliki secara sah.", 13f))
        body.addView(about)
        setContentView(Ui.scroll(this, body))
        if (intent.getBooleanExtra("autotest", false)) {
            bg.execute {
                val j = JSONObject(NativeBridge.selfTest(App.homebrewXex.path))
                AppLog.i("AUTOTEST selftest ok=${j.optBoolean("ok")} matched=${j.optInt("matched")}/${j.optInt("total")} " +
                    "vmx128_failures=${j.optInt("vmx128_failures")} native_us=${j.optDouble("native_us")} recomp_us=${j.optDouble("recomp_us")} error=${j.optString("error")}")
                AppLog.i("AUTOTEST gpu ${NativeBridge.gpuInfo()}")
                // One ISO as the only input: scan through a file descriptor, then guest file I/O from the ISO.
                val t0 = System.currentTimeMillis()
                val scan = JSONObject(NativeBridge.scanDisc(android.os.ParcelFileDescriptor.open(App.homebrewIso,
                    android.os.ParcelFileDescriptor.MODE_READ_ONLY).detachFd(), null, true, null))
                val ok = (0 until (scan.optJSONArray("requirements")?.length() ?: 0)).count {
                    scan.getJSONArray("requirements").getJSONObject(it).optString("state") == "ok"
                }
                AppLog.i("AUTOTEST scan status=${scan.optString("status")} layout=${scan.optString("layout")} files=${scan.optLong("files")} " +
                    "main=${scan.optString("main_xex")} requirements_ok=$ok ms=${System.currentTimeMillis() - t0} reason=${scan.optString("status_reason")}")
                val ft = JSONObject(NativeBridge.discFileTest(android.os.ParcelFileDescriptor.open(App.homebrewIso,
                    android.os.ParcelFileDescriptor.MODE_READ_ONLY).detachFd()))
                AppLog.i("AUTOTEST discio passed=${ft.optInt("passed")}/${ft.optInt("total")} bytes=${ft.optLong("bytes")} error=${ft.optString("error")}")
                AppLog.pullNative()
                runOnUiThread { finish() }
            }
        }
    }

    @Deprecated("framework API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: android.content.Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_EXPORT || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        runCatching {
            contentResolver.openOutputStream(uri)!!.use { it.write(App.homebrewIso.readBytes()) }
            Ui.info(this, "ISO uji diekspor", "Sekarang pilih file ini lewat \"Tambah ISO\" di Library untuk mencoba alur SAF.")
        }.onFailure { Ui.info(this, "Ekspor gagal", it.toString()) }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (toneOn) NativeBridge.audioTestTone(false)
        bg.shutdown()
    }

    companion object { const val REQ_EXPORT = 61 }
}
