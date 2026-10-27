package com.aldiianshr.fifastreet

import android.app.ActivityManager
import android.app.Application
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        instance = this
        installHomebrew()
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try {
                AppLog.report("crash", Log.getStackTraceString(e))
            } catch (_: Throwable) {
            }
            prev?.uncaughtException(t, e)
        }
        AppLog.i("App start: ${DeviceInfo.summary(this)}")
    }

    /** Copies the bundled homebrew test XEX (our own code) out of the APK. */
    private fun installHomebrew() {
        val dir = File(filesDir, "homebrew").apply { mkdirs() }
        val out = File(dir, "hb64.xex")
        assets.open("homebrew/hb64.xex").use { input ->
            val bytes = input.readBytes()
            if (!out.exists() || out.length() != bytes.size.toLong() || !out.readBytes().contentEquals(bytes)) {
                out.writeBytes(bytes)
            }
        }
    }

    companion object {
        lateinit var instance: App
            private set
        val homebrewXex: File get() = File(instance.filesDir, "homebrew/hb64.xex")
        val keyFile: File get() = File(instance.filesDir, "keys/xex.key")
        val cacheRoot: File get() = File(instance.filesDir, "analysis").apply { mkdirs() }
        val reportsDir: File get() = File(instance.filesDir, "reports").apply { mkdirs() }
    }
}

/** App-side log that merges with the native ring buffer. */
object AppLog {
    private val lines = ArrayDeque<String>()
    private val fmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)

    @Synchronized
    fun i(msg: String) {
        Log.i("FSR", msg)
        lines.addLast("${fmt.format(Date())} $msg")
        while (lines.size > 3000) lines.removeFirst()
    }

    @Synchronized
    fun pullNative() {
        val n = try { NativeBridge.drainLog() } catch (_: Throwable) { "" }
        if (n.isNotEmpty()) n.trimEnd().split('\n').forEach { lines.addLast("${fmt.format(Date())} $it") }
        while (lines.size > 3000) lines.removeFirst()
    }

    @Synchronized
    fun snapshot(): List<String> {
        pullNative()
        return lines.toList()
    }

    @Synchronized
    fun clear() = lines.clear()

    /** Writes an error report file (kept in app storage, shown in the Log screen). */
    fun report(kind: String, body: String): File {
        val f = File(App.reportsDir, "${kind}_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())}.txt")
        val text = buildString {
            appendLine("FIFA STREET Recompiled ${BuildConfigCompat.versionName()} report: $kind")
            appendLine(DeviceInfo.summary(App.instance))
            appendLine()
            appendLine(body)
            appendLine()
            appendLine("---- recent log ----")
            snapshot().takeLast(300).forEach { appendLine(it) }
        }
        f.writeText(text)
        i("Report saved: ${f.name}")
        return f
    }
}

object BuildConfigCompat {
    fun versionName(): String = try {
        val ctx = App.instance
        ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName ?: "?"
    } catch (_: Exception) { "?" }
}

/** Automatic device capability detection. */
object DeviceInfo {
    fun soc(): String = if (Build.VERSION.SDK_INT >= 31) "${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else Build.HARDWARE

    fun totalRamMb(ctx: Context): Long {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mi = ActivityManager.MemoryInfo()
        am.getMemoryInfo(mi)
        return mi.totalMem / (1024 * 1024)
    }

    fun vulkanLevel(ctx: Context): String {
        val pm = ctx.packageManager
        val v = pm.systemAvailableFeatures.firstOrNull { it.name == PackageManager.FEATURE_VULKAN_HARDWARE_VERSION }?.version ?: 0
        if (v == 0) return "tidak ada"
        return "${v shr 22}.${(v shr 12) and 0x3FF}"
    }

    fun isXiaomiPad6(): Boolean =
        Build.MODEL.contains("23043RP34", true) || Build.DEVICE.equals("pipa", true) ||
            Build.MODEL.contains("Pad 6", true)

    fun isSnapdragon870(): Boolean = soc().contains("SM8250", true) || Build.HARDWARE.contains("kona", true)

    fun summary(ctx: Context): String =
        "${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE}), Android ${Build.VERSION.RELEASE} / API ${Build.VERSION.SDK_INT}, " +
            "SoC ${soc()}, ${Runtime.getRuntime().availableProcessors()} core, RAM ${totalRamMb(ctx)} MB, Vulkan ${vulkanLevel(ctx)}, " +
            "ABI ${Build.SUPPORTED_ABIS.joinToString()}"
}
