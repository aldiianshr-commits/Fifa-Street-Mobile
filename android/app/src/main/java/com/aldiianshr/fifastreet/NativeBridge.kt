package com.aldiianshr.fifastreet

import android.view.Surface

/** JNI surface of libfsr.so (see app/src/main/cpp/jni_bridge.cpp). */
object NativeBridge {
    init {
        System.loadLibrary("fsr")
    }

    // Container / XEX analysis
    external fun analyzeXex(path: String, keyPath: String?, deep: Boolean): String
    external fun sniff(path: String): Int // 0 unknown, 1 XEX, 2 XDVDFS ISO, 3 STFS (GOD/LIVE/CON)
    external fun isoListJson(path: String): String
    external fun isoExtract(isoPath: String, entry: String, outPath: String): Boolean
    external fun modulesJson(): String
    external fun hleJson(): String
    external fun gpuInfo(): String
    external fun selfTest(xexPath: String): String

    // Sessions (one running title)
    external fun sessionCreate(xexPath: String, keyPath: String?): Long
    external fun lastCreateError(): String
    external fun sessionSetSurface(h: Long, surface: Surface?)
    external fun sessionStart(h: Long)
    external fun sessionPause(h: Long)
    external fun sessionResume(h: Long)
    external fun sessionStop(h: Long)
    external fun sessionDestroy(h: Long)
    external fun sessionRunning(h: Long): Boolean
    external fun sessionSetPad(
        h: Long, buttons: Int, lt: Int, rt: Int, lx: Int, ly: Int, rx: Int, ry: Int, connected: Boolean
    )
    external fun sessionSetGraphics(
        h: Long, width: Int, height: Int, scalePct: Int, fps: Int, vsync: Boolean, linear: Boolean,
        aspect: Int, thermalAdapt: Boolean
    )
    external fun sessionSetThermal(h: Long, status: Int)
    external fun sessionStats(h: Long): FloatArray
    external fun sessionError(h: Long): String
    external fun sessionGpu(h: Long): String

    // Misc
    external fun drainLog(): String
    external fun audioTestTone(on: Boolean): Boolean

    const val KIND_UNKNOWN = 0
    const val KIND_XEX = 1
    const val KIND_ISO = 2
    const val KIND_STFS = 3
}
