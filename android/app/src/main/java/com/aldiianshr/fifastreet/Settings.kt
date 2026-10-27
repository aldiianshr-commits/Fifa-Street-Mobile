package com.aldiianshr.fifastreet

import android.content.Context
import android.view.KeyEvent
import org.json.JSONArray
import org.json.JSONObject

/** Xbox 360 pad buttons (XINPUT_GAMEPAD_* bit values, as seen by the guest). */
enum class XButton(val bit: Int, val label: String) {
    DPAD_UP(0x0001, "D-Pad ↑"), DPAD_DOWN(0x0002, "D-Pad ↓"), DPAD_LEFT(0x0004, "D-Pad ←"), DPAD_RIGHT(0x0008, "D-Pad →"),
    START(0x0010, "Start"), BACK(0x0020, "Back"), LS(0x0040, "LS (klik)"), RS(0x0080, "RS (klik)"),
    LB(0x0100, "LB"), RB(0x0200, "RB"), A(0x1000, "A"), B(0x2000, "B"), X(0x4000, "X"), Y(0x8000, "Y"),
    LT(0x10000, "LT"), RT(0x20000, "RT"); // triggers are analog in XInput; bit only used for remapping
}

data class GraphicsSettings(
    var resolution: Int = 720,      // 480 / 720 / 1080 (16:9 internal resolution)
    var scalePct: Int = 100,        // render scale
    var fps: Int = 60,              // 30 / 60
    var vsync: Boolean = true,
    var linear: Boolean = true,     // nearest / linear filtering of the final image
    var aspect: Int = 0,            // 0 = 16:9, 1 = stretch
    var thermalAdapt: Boolean = true,
    var showStats: Boolean = true,
    var profile: String = "Balanced",
) {
    val width: Int get() = when (resolution) { 480 -> 854; 1080 -> 1920; else -> 1280 }
    val height: Int get() = when (resolution) { 480 -> 480; 1080 -> 1080; else -> 720 }

    fun applyProfile(p: String) {
        profile = p
        when (p) {
            "Performance" -> { resolution = 480; scalePct = 100; fps = 60; linear = true; vsync = true }
            "Quality" -> { resolution = 1080; scalePct = 100; fps = 60; linear = true; vsync = true }
            else -> { resolution = 720; scalePct = 100; fps = 60; linear = true; vsync = true }
        }
    }

    fun toJson() = JSONObject().apply {
        put("resolution", resolution); put("scalePct", scalePct); put("fps", fps); put("vsync", vsync)
        put("linear", linear); put("aspect", aspect); put("thermalAdapt", thermalAdapt); put("showStats", showStats)
        put("profile", profile)
    }

    companion object {
        fun fromJson(o: JSONObject?) = GraphicsSettings().apply {
            if (o == null) return@apply
            resolution = o.optInt("resolution", resolution); scalePct = o.optInt("scalePct", scalePct)
            fps = o.optInt("fps", fps); vsync = o.optBoolean("vsync", vsync); linear = o.optBoolean("linear", linear)
            aspect = o.optInt("aspect", aspect); thermalAdapt = o.optBoolean("thermalAdapt", thermalAdapt)
            showStats = o.optBoolean("showStats", showStats); profile = o.optString("profile", profile)
        }

        /** Default chosen from detected hardware. */
        fun deviceDefault(ctx: Context): GraphicsSettings {
            val g = GraphicsSettings()
            val ram = DeviceInfo.totalRamMb(ctx)
            g.applyProfile(if (DeviceInfo.isSnapdragon870() || ram >= 6000) "Balanced" else "Performance")
            return g
        }
    }
}

/** One on-screen control: normalised centre position and size multiplier. */
data class ControlLayoutItem(val id: String, var x: Float, var y: Float, var size: Float = 1f)

data class ControlProfile(
    var name: String = "Default",
    var opacity: Float = 0.55f,
    var stickSensitivity: Float = 1.0f,
    var deadzone: Float = 0.15f,
    var gamepadSensitivity: Float = 1.0f,
    var swapSticks: Boolean = false,
    var invertLeftY: Boolean = false,
    var invertRightY: Boolean = false,
    var haptics: Boolean = true,
    var showTouch: Boolean = true,
    val layout: MutableList<ControlLayoutItem> = defaultLayout(),
    val keyMap: MutableMap<Int, XButton> = defaultKeyMap(),
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name); put("opacity", opacity.toDouble()); put("stickSensitivity", stickSensitivity.toDouble())
        put("deadzone", deadzone.toDouble()); put("gamepadSensitivity", gamepadSensitivity.toDouble())
        put("swapSticks", swapSticks); put("invertLeftY", invertLeftY); put("invertRightY", invertRightY)
        put("haptics", haptics); put("showTouch", showTouch)
        put("layout", JSONArray().apply {
            layout.forEach { put(JSONObject().put("id", it.id).put("x", it.x.toDouble()).put("y", it.y.toDouble()).put("size", it.size.toDouble())) }
        })
        put("keyMap", JSONObject().apply { keyMap.forEach { (k, v) -> put(k.toString(), v.name) } })
    }

    fun copyAs(newName: String): ControlProfile = fromJson(toJson()).also { it.name = newName }

    companion object {
        fun defaultLayout() = mutableListOf(
            ControlLayoutItem("lstick", 0.14f, 0.68f, 1.0f),
            ControlLayoutItem("rstick", 0.70f, 0.78f, 0.85f),
            ControlLayoutItem("dpad", 0.30f, 0.80f, 0.9f),
            ControlLayoutItem("A", 0.88f, 0.70f), ControlLayoutItem("B", 0.95f, 0.55f),
            ControlLayoutItem("X", 0.81f, 0.55f), ControlLayoutItem("Y", 0.88f, 0.40f),
            ControlLayoutItem("LB", 0.10f, 0.16f), ControlLayoutItem("LT", 0.10f, 0.32f),
            ControlLayoutItem("RB", 0.90f, 0.16f), ControlLayoutItem("RT", 0.78f, 0.22f),
            ControlLayoutItem("BACK", 0.42f, 0.08f, 0.8f), ControlLayoutItem("START", 0.58f, 0.08f, 0.8f),
            ControlLayoutItem("LS", 0.24f, 0.50f, 0.7f), ControlLayoutItem("RS", 0.60f, 0.62f, 0.7f),
        )

        fun defaultKeyMap(): MutableMap<Int, XButton> = mutableMapOf(
            KeyEvent.KEYCODE_BUTTON_A to XButton.A, KeyEvent.KEYCODE_BUTTON_B to XButton.B,
            KeyEvent.KEYCODE_BUTTON_X to XButton.X, KeyEvent.KEYCODE_BUTTON_Y to XButton.Y,
            KeyEvent.KEYCODE_BUTTON_L1 to XButton.LB, KeyEvent.KEYCODE_BUTTON_R1 to XButton.RB,
            KeyEvent.KEYCODE_BUTTON_L2 to XButton.LT, KeyEvent.KEYCODE_BUTTON_R2 to XButton.RT,
            KeyEvent.KEYCODE_BUTTON_THUMBL to XButton.LS, KeyEvent.KEYCODE_BUTTON_THUMBR to XButton.RS,
            KeyEvent.KEYCODE_BUTTON_START to XButton.START, KeyEvent.KEYCODE_BUTTON_SELECT to XButton.BACK,
            KeyEvent.KEYCODE_DPAD_UP to XButton.DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN to XButton.DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to XButton.DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT to XButton.DPAD_RIGHT,
        )

        fun preset(name: String): ControlProfile = when (name) {
            "Ringkas" -> ControlProfile(name = name, opacity = 0.45f).also { p -> p.layout.forEach { it.size *= 0.8f } }
            "Besar" -> ControlProfile(name = name, opacity = 0.6f).also { p -> p.layout.forEach { it.size *= 1.2f } }
            else -> ControlProfile(name = "Default")
        }

        fun fromJson(o: JSONObject): ControlProfile {
            val p = ControlProfile()
            p.name = o.optString("name", p.name)
            p.opacity = o.optDouble("opacity", p.opacity.toDouble()).toFloat()
            p.stickSensitivity = o.optDouble("stickSensitivity", 1.0).toFloat()
            p.deadzone = o.optDouble("deadzone", 0.15).toFloat()
            p.gamepadSensitivity = o.optDouble("gamepadSensitivity", 1.0).toFloat()
            p.swapSticks = o.optBoolean("swapSticks"); p.invertLeftY = o.optBoolean("invertLeftY")
            p.invertRightY = o.optBoolean("invertRightY"); p.haptics = o.optBoolean("haptics", true)
            p.showTouch = o.optBoolean("showTouch", true)
            o.optJSONArray("layout")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val it = arr.getJSONObject(i)
                    p.layout.firstOrNull { l -> l.id == it.optString("id") }?.apply {
                        x = it.optDouble("x", x.toDouble()).toFloat(); y = it.optDouble("y", y.toDouble()).toFloat()
                        size = it.optDouble("size", size.toDouble()).toFloat()
                    }
                }
            }
            o.optJSONObject("keyMap")?.let { km ->
                p.keyMap.clear()
                km.keys().forEach { k -> runCatching { p.keyMap[k.toInt()] = XButton.valueOf(km.getString(k)) } }
            }
            return p
        }
    }
}

/** Persistent configuration, saved automatically on every change. */
object Store {
    private val prefs by lazy { App.instance.getSharedPreferences("fsr", Context.MODE_PRIVATE) }

    var gameFolder: String?
        get() = prefs.getString("gameFolder", null)
        set(v) = prefs.edit().putString("gameFolder", v).apply()

    fun globalGraphics(): GraphicsSettings =
        prefs.getString("gfx", null)?.let { GraphicsSettings.fromJson(JSONObject(it)) }
            ?: GraphicsSettings.deviceDefault(App.instance)

    fun saveGlobalGraphics(g: GraphicsSettings) = prefs.edit().putString("gfx", g.toJson().toString()).apply()

    /** Per-game graphics override (null = use global). */
    fun gameGraphics(gameId: String): GraphicsSettings? =
        prefs.getString("gfx_$gameId", null)?.let { GraphicsSettings.fromJson(JSONObject(it)) }

    fun saveGameGraphics(gameId: String, g: GraphicsSettings?) =
        prefs.edit().apply { if (g == null) remove("gfx_$gameId") else putString("gfx_$gameId", g.toJson().toString()) }.apply()

    fun effectiveGraphics(gameId: String) = gameGraphics(gameId) ?: globalGraphics()

    fun profiles(): MutableList<ControlProfile> {
        val arr = prefs.getString("profiles", null)?.let { JSONArray(it) }
        val list = mutableListOf<ControlProfile>()
        if (arr != null) for (i in 0 until arr.length()) list += ControlProfile.fromJson(arr.getJSONObject(i))
        if (list.isEmpty()) list += ControlProfile()
        return list
    }

    fun saveProfiles(list: List<ControlProfile>) =
        prefs.edit().putString("profiles", JSONArray().apply { list.forEach { put(it.toJson()) } }.toString()).apply()

    var activeProfile: String
        get() = prefs.getString("activeProfile", "Default") ?: "Default"
        set(v) = prefs.edit().putString("activeProfile", v).apply()

    fun gameProfileName(gameId: String): String = prefs.getString("profile_$gameId", null) ?: activeProfile
    fun setGameProfileName(gameId: String, name: String) = prefs.edit().putString("profile_$gameId", name).apply()

    fun profileFor(gameId: String?): ControlProfile {
        val name = if (gameId != null) gameProfileName(gameId) else activeProfile
        return profiles().firstOrNull { it.name == name } ?: profiles().first()
    }

    fun upsertProfile(p: ControlProfile) {
        val list = profiles()
        val i = list.indexOfFirst { it.name == p.name }
        if (i >= 0) list[i] = p else list += p
        saveProfiles(list)
    }
}
