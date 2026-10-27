package com.aldiianshr.fifastreet

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/** Xbox-360-layout touch controller. Produces XInput-style state. */
class PadInput {
    var buttons = 0
    var lt = 0
    var rt = 0
    var lx = 0
    var ly = 0
    var rx = 0
    var ry = 0
    fun copyFrom(o: PadInput) { buttons = o.buttons; lt = o.lt; rt = o.rt; lx = o.lx; ly = o.ly; rx = o.rx; ry = o.ry }
    fun same(o: PadInput) = buttons == o.buttons && lt == o.lt && rt == o.rt && lx == o.lx && ly == o.ly && rx == o.rx && ry == o.ry
}

class VirtualControllerView(ctx: Context) : View(ctx) {
    var profile: ControlProfile = ControlProfile()
        set(v) { field = v; invalidate() }
    var editMode = false
    var selectedId: String? = null
        private set
    var onState: ((PadInput) -> Unit)? = null
    var onSelect: ((String) -> Unit)? = null
    var onLayoutChanged: (() -> Unit)? = null

    val state = PadInput()
    private val pointers = HashMap<Int, String>()          // pointer id -> element id
    private val stickOrigin = HashMap<String, Pair<Float, Float>>()
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private var dragDx = 0f
    private var dragDy = 0f

    private val unit get() = min(width, height) * 0.085f

    private fun item(id: String) = profile.layout.firstOrNull { it.id == id }
    private fun cx(i: ControlLayoutItem) = i.x * width
    private fun cy(i: ControlLayoutItem) = i.y * height

    /** Radius / half-size of each element in px. */
    private fun radius(i: ControlLayoutItem): Float = unit * i.size * when (i.id) {
        "lstick", "rstick" -> 1.55f
        "dpad" -> 1.35f
        "A", "B", "X", "Y" -> 0.62f
        "LB", "RB", "LT", "RT" -> 0.7f
        "BACK", "START" -> 0.5f
        else -> 0.5f
    }

    private fun buttonBit(id: String): Int = when (id) {
        "A" -> XButton.A.bit; "B" -> XButton.B.bit; "X" -> XButton.X.bit; "Y" -> XButton.Y.bit
        "LB" -> XButton.LB.bit; "RB" -> XButton.RB.bit; "BACK" -> XButton.BACK.bit; "START" -> XButton.START.bit
        "LS" -> XButton.LS.bit; "RS" -> XButton.RS.bit
        else -> 0
    }

    private fun faceColor(id: String) = when (id) {
        "A" -> 0x3CD25A; "B" -> 0xE63C3C; "X" -> 0x3C78F0; "Y" -> 0xF0D23C
        else -> 0xB0B8C8
    }

    private fun hit(x: Float, y: Float): String? {
        var best: String? = null
        var bestD = Float.MAX_VALUE
        for (i in profile.layout) {
            val d = hypot(x - cx(i), y - cy(i))
            val r = radius(i) * 1.15f
            if (d <= r && d < bestD) { best = i.id; bestD = d }
        }
        return best
    }

    override fun onDraw(c: Canvas) {
        if (!profile.showTouch && !editMode) return
        val alpha = (profile.opacity * 255).toInt().coerceIn(30, 255)
        label.textSize = unit * 0.45f
        for (i in profile.layout) {
            val x = cx(i); val y = cy(i); val r = radius(i)
            val pressed = isPressed(i.id)
            val sel = editMode && i.id == selectedId
            when (i.id) {
                "lstick", "rstick" -> {
                    fill.color = Color.argb(alpha / 2, 40, 50, 70); c.drawCircle(x, y, r, fill)
                    stroke.color = Color.argb(alpha, 180, 190, 210); c.drawCircle(x, y, r, stroke)
                    val (sx, sy) = if (i.id == "lstick") state.lx to state.ly else state.rx to state.ry
                    val kx = x + sx / 32767f * r * 0.6f
                    val ky = y - sy / 32767f * r * 0.6f
                    fill.color = Color.argb(alpha, 200, 210, 230); c.drawCircle(kx, ky, r * 0.42f, fill)
                    c.drawText(if (i.id == "lstick") "L" else "R", kx, ky + label.textSize / 3, label.apply { this.alpha = alpha })
                }
                "dpad" -> {
                    val w = r * 0.36f
                    fill.color = Color.argb(alpha / 2, 40, 50, 70)
                    c.drawRoundRect(RectF(x - w, y - r, x + w, y + r), 8f, 8f, fill)
                    c.drawRoundRect(RectF(x - r, y - w, x + r, y + w), 8f, 8f, fill)
                    fill.color = Color.argb(alpha, 230, 230, 240)
                    val b = state.buttons
                    if (b and XButton.DPAD_UP.bit != 0) c.drawRect(x - w, y - r, x + w, y - w, fill)
                    if (b and XButton.DPAD_DOWN.bit != 0) c.drawRect(x - w, y + w, x + w, y + r, fill)
                    if (b and XButton.DPAD_LEFT.bit != 0) c.drawRect(x - r, y - w, x - w, y + w, fill)
                    if (b and XButton.DPAD_RIGHT.bit != 0) c.drawRect(x + w, y - w, x + r, y + w, fill)
                    stroke.color = Color.argb(alpha, 180, 190, 210)
                    c.drawRoundRect(RectF(x - w, y - r, x + w, y + r), 8f, 8f, stroke)
                    c.drawRoundRect(RectF(x - r, y - w, x + r, y + w), 8f, 8f, stroke)
                }
                else -> {
                    val col = faceColor(i.id)
                    val a = if (pressed) 255 else alpha
                    fill.color = Color.argb(if (pressed) 230 else alpha / 2, (col shr 16) and 255, (col shr 8) and 255, col and 255)
                    if (i.id in listOf("LB", "RB", "LT", "RT", "BACK", "START")) {
                        c.drawRoundRect(RectF(x - r * 1.3f, y - r * 0.7f, x + r * 1.3f, y + r * 0.7f), r * 0.5f, r * 0.5f, fill)
                        stroke.color = Color.argb(a, 220, 225, 235)
                        c.drawRoundRect(RectF(x - r * 1.3f, y - r * 0.7f, x + r * 1.3f, y + r * 0.7f), r * 0.5f, r * 0.5f, stroke)
                    } else {
                        c.drawCircle(x, y, r, fill)
                        stroke.color = Color.argb(a, 220, 225, 235); c.drawCircle(x, y, r, stroke)
                    }
                    label.alpha = a
                    c.drawText(i.id, x, y + label.textSize / 3, label)
                }
            }
            if (sel) {
                stroke.color = Color.argb(255, 47, 128, 237)
                stroke.strokeWidth = 5f
                c.drawCircle(x, y, r * 1.2f, stroke)
                stroke.strokeWidth = 3f
            }
        }
    }

    private fun isPressed(id: String): Boolean = when (id) {
        "LT" -> state.lt > 0
        "RT" -> state.rt > 0
        else -> buttonBit(id) != 0 && state.buttons and buttonBit(id) != 0
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (editMode) return editTouch(e)
        val idx = e.actionIndex
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val id = hit(e.getX(idx), e.getY(idx)) ?: return true
                pointers[e.getPointerId(idx)] = id
                if (id == "lstick" || id == "rstick") stickOrigin[id] = item(id)!!.let { cx(it) to cy(it) }
                if (profile.haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                recompute(e)
            }
            MotionEvent.ACTION_MOVE -> recompute(e)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) pointers.clear() else pointers.remove(e.getPointerId(idx))
                recompute(e, excludeIndex = if (e.actionMasked == MotionEvent.ACTION_CANCEL) -2 else idx)
            }
        }
        return true
    }

    private fun recompute(e: MotionEvent, excludeIndex: Int = -1) {
        var b = 0; var lt = 0; var rt = 0; var lx = 0; var ly = 0; var rx = 0; var ry = 0
        if (excludeIndex != -2) for (p in 0 until e.pointerCount) {
            if (p == excludeIndex) continue
            val id = pointers[e.getPointerId(p)] ?: continue
            val it = item(id) ?: continue
            val x = e.getX(p); val y = e.getY(p)
            when (id) {
                "lstick", "rstick" -> {
                    val r = radius(it) * 0.6f
                    var dx = (x - cx(it)) / r * profile.stickSensitivity
                    var dy = -(y - cy(it)) / r * profile.stickSensitivity
                    val m = hypot(dx, dy)
                    if (m > 1f) { dx /= m; dy /= m }
                    if (m < profile.deadzone) { dx = 0f; dy = 0f }
                    val vx = (dx * 32767).toInt(); val vy = (dy * 32767).toInt()
                    if (id == "lstick") { lx = vx; ly = vy } else { rx = vx; ry = vy }
                }
                "dpad" -> {
                    val dx = x - cx(it); val dy = y - cy(it)
                    if (hypot(dx, dy) > radius(it) * 0.2f) {
                        val ang = Math.toDegrees(atan2(-dy.toDouble(), dx.toDouble()))
                        // 8-way with 45° sectors
                        if (ang in -67.5..67.5) b = b or XButton.DPAD_RIGHT.bit
                        if (ang in 22.5..157.5) b = b or XButton.DPAD_UP.bit
                        if (ang >= 112.5 || ang <= -112.5) b = b or XButton.DPAD_LEFT.bit
                        if (ang in -157.5..-22.5) b = b or XButton.DPAD_DOWN.bit
                    }
                }
                "LT" -> lt = 255
                "RT" -> rt = 255
                else -> b = b or buttonBit(id)
            }
        }
        val changed = b != state.buttons || lt != state.lt || rt != state.rt || lx != state.lx || ly != state.ly || rx != state.rx || ry != state.ry
        state.buttons = b; state.lt = lt; state.rt = rt; state.lx = lx; state.ly = ly; state.rx = rx; state.ry = ry
        if (changed) { onState?.invoke(state); invalidate() }
    }

    private fun editTouch(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val id = hit(e.x, e.y)
                selectedId = id
                if (id != null) {
                    val it = item(id)!!
                    dragDx = e.x - cx(it); dragDy = e.y - cy(it)
                    onSelect?.invoke(id)
                }
                invalidate()
            }
            MotionEvent.ACTION_MOVE -> {
                val it = selectedId?.let { item(it) } ?: return true
                it.x = ((e.x - dragDx) / width).coerceIn(0.03f, 0.97f)
                it.y = ((e.y - dragDy) / height).coerceIn(0.05f, 0.95f)
                invalidate()
            }
            MotionEvent.ACTION_UP -> onLayoutChanged?.invoke()
        }
        return true
    }

    fun select(id: String?) { selectedId = id; invalidate() }
}
