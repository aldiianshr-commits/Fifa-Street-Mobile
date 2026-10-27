package com.aldiianshr.fifastreet

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs
import kotlin.math.hypot

/** Bluetooth/USB gamepad -> XInput state, with remapping, deadzone and sensitivity. */
class Gamepad(var profile: ControlProfile) {
    val state = PadInput()
    private var keyButtons = 0
    private var hatButtons = 0
    private var keyLt = 0
    private var keyRt = 0
    private var axisLt = 0
    private var axisRt = 0
    var connected = false
        private set

    fun isGamepad(dev: InputDevice?): Boolean {
        val s = dev?.sources ?: return false
        return (s and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (s and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    /** Returns true if the event was consumed. */
    fun onKey(e: KeyEvent): Boolean {
        if (!isGamepad(e.device) && !KeyEvent.isGamepadButton(e.keyCode)) return false
        val xb = profile.keyMap[e.keyCode] ?: return false
        connected = true
        val down = e.action == KeyEvent.ACTION_DOWN
        when (xb) {
            XButton.LT -> keyLt = if (down) 255 else 0
            XButton.RT -> keyRt = if (down) 255 else 0
            else -> keyButtons = if (down) keyButtons or xb.bit else keyButtons and xb.bit.inv()
        }
        update()
        return true
    }

    fun onMotion(e: MotionEvent): Boolean {
        if (!isGamepad(e.device) || e.action != MotionEvent.ACTION_MOVE) return false
        connected = true
        var lx = axis(e, MotionEvent.AXIS_X)
        var ly = -axis(e, MotionEvent.AXIS_Y)
        var rx = axis(e, MotionEvent.AXIS_Z)
        var ry = -axis(e, MotionEvent.AXIS_RZ)
        if (profile.invertLeftY) ly = -ly
        if (profile.invertRightY) ry = -ry
        if (profile.swapSticks) { val tx = lx; val ty = ly; lx = rx; ly = ry; rx = tx; ry = ty }
        val (alx, aly) = shape(lx, ly)
        val (arx, ary) = shape(rx, ry)
        state.lx = alx; state.ly = aly; state.rx = arx; state.ry = ary
        val l = maxOf(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE))
        val r = maxOf(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS))
        axisLt = (l.coerceIn(0f, 1f) * 255).toInt()
        axisRt = (r.coerceIn(0f, 1f) * 255).toInt()
        val hx = e.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y)
        hatButtons = 0
        if (hx < -0.5f) hatButtons = hatButtons or XButton.DPAD_LEFT.bit
        if (hx > 0.5f) hatButtons = hatButtons or XButton.DPAD_RIGHT.bit
        if (hy < -0.5f) hatButtons = hatButtons or XButton.DPAD_UP.bit
        if (hy > 0.5f) hatButtons = hatButtons or XButton.DPAD_DOWN.bit
        update()
        return true
    }

    private fun axis(e: MotionEvent, a: Int): Float {
        val range = e.device?.getMotionRange(a, e.source)
        val v = e.getAxisValue(a)
        val flat = range?.flat ?: 0f
        return if (abs(v) <= flat) 0f else v
    }

    /** Radial deadzone + sensitivity, mapped to signed 16-bit. */
    private fun shape(x: Float, y: Float): Pair<Int, Int> {
        val m = hypot(x, y)
        val dz = profile.deadzone
        if (m < dz) return 0 to 0
        val scaled = ((m - dz) / (1f - dz) * profile.gamepadSensitivity).coerceAtMost(1f)
        val nx = x / m * scaled
        val ny = y / m * scaled
        return (nx * 32767).toInt().coerceIn(-32768, 32767) to (ny * 32767).toInt().coerceIn(-32768, 32767)
    }

    private fun update() {
        state.buttons = keyButtons or hatButtons
        state.lt = maxOf(keyLt, axisLt)
        state.rt = maxOf(keyRt, axisRt)
    }

    companion object {
        fun keyName(code: Int): String = KeyEvent.keyCodeToString(code).removePrefix("KEYCODE_")

        fun merge(a: PadInput, b: PadInput, out: PadInput) {
            out.buttons = a.buttons or b.buttons
            out.lt = maxOf(a.lt, b.lt)
            out.rt = maxOf(a.rt, b.rt)
            fun pick(x1: Int, y1: Int, x2: Int, y2: Int) =
                if (x1.toLong() * x1 + y1.toLong() * y1 >= x2.toLong() * x2 + y2.toLong() * y2) x1 to y1 else x2 to y2
            val (lx, ly) = pick(a.lx, a.ly, b.lx, b.ly)
            val (rx, ry) = pick(a.rx, a.ry, b.rx, b.ry)
            out.lx = lx; out.ly = ly; out.rx = rx; out.ry = ry
        }
    }
}
