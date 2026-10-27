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
        row.addView(Ui.button(this, "Uji runtime (homebrew, native vs recompile)", true) {
            result.text = "Menjalankan…"
            bg.execute {
                val j = JSONObject(NativeBridge.selfTest(App.homebrewXex.path))
                val txt = if (!j.optBoolean("ok")) "Gagal: ${j.optString("error")}" else {
                    val res = j.getJSONArray("results")
                    buildString {
                        append("Hasil identik: ${j.optInt("matched")}/20  (hash native ${j.optString("hash_native")}, recompile ${j.optString("hash_recomp")})\n")
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
        rt.addView(result)
        rt.addView(Ui.text(this, "Audio game (XAudio2/XMA) belum tersedia; tombol di atas hanya menguji jalur audio perangkat.", 12f, Ui.DIM))
        body.addView(rt)

        val about = Ui.card(this)
        about.addView(Ui.section(this, "TENTANG"))
        about.addView(Ui.text(this, "FIFA STREET Recompiled ${BuildConfigCompat.versionName()} – proyek komunitas, tidak berafiliasi dengan EA, FIFA, atau Microsoft. " +
            "Tidak ada file game, firmware, atau kunci dekripsi yang disertakan. Gunakan hanya salinan game yang Anda miliki secara sah.", 13f))
        body.addView(about)
        setContentView(Ui.scroll(this, body))
    }

    override fun onDestroy() {
        super.onDestroy()
        if (toneOn) NativeBridge.audioTestTone(false)
        bg.shutdown()
    }
}
