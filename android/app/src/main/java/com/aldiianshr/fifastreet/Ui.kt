package com.aldiianshr.fifastreet

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView

/** Tiny programmatic UI toolkit (no AndroidX dependency). */
object Ui {
    const val BG = 0xFF0E1420.toInt()
    const val SURFACE = 0xFF172133.toInt()
    const val ACCENT = 0xFF2F80ED.toInt()
    const val TEXT = 0xFFE8EDF5.toInt()
    const val DIM = 0xFF93A1B8.toInt()
    const val OK = 0xFF27AE60.toInt()
    const val PARTIAL = 0xFFF2C94C.toInt()
    const val NO = 0xFFEB5757.toInt()

    fun dp(ctx: Context, v: Float) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, ctx.resources.displayMetrics)
    fun dpi(ctx: Context, v: Int) = dp(ctx, v.toFloat()).toInt()

    fun rounded(color: Int, radius: Float, stroke: Int = 0, strokeColor: Int = 0) = GradientDrawable().apply {
        setColor(color); cornerRadius = radius
        if (stroke > 0) setStroke(stroke, strokeColor)
    }

    fun vbox(ctx: Context, pad: Int = 12) = LinearLayout(ctx).apply {
        orientation = LinearLayout.VERTICAL
        val p = dpi(ctx, pad); setPadding(p, p, p, p)
    }

    fun hbox(ctx: Context) = LinearLayout(ctx).apply {
        orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
    }

    fun scroll(ctx: Context, child: View) = ScrollView(ctx).apply { addView(child); isFillViewport = true }

    fun card(ctx: Context): LinearLayout = vbox(ctx, 14).apply {
        background = rounded(SURFACE, dp(ctx, 12f))
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            bottomMargin = dpi(ctx, 10)
        }
    }

    fun text(ctx: Context, s: CharSequence, size: Float = 15f, color: Int = TEXT, bold: Boolean = false) = TextView(ctx).apply {
        text = s; setTextColor(color); textSize = size
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    fun title(ctx: Context, s: String) = text(ctx, s, 20f, TEXT, true).apply { setPadding(0, 0, 0, dpi(ctx, 8)) }
    fun section(ctx: Context, s: String) = text(ctx, s, 13f, ACCENT, true).apply { setPadding(0, dpi(ctx, 6), 0, dpi(ctx, 4)) }

    fun button(ctx: Context, s: String, primary: Boolean = false, onClick: (View) -> Unit) = Button(ctx).apply {
        text = s; isAllCaps = false; setTextColor(if (primary) Color.WHITE else TEXT)
        background = rounded(if (primary) ACCENT else 0xFF243149.toInt(), dp(ctx, 10f))
        setPadding(dpi(ctx, 16), dpi(ctx, 8), dpi(ctx, 16), dpi(ctx, 8))
        setOnClickListener(onClick)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            marginEnd = dpi(ctx, 8); topMargin = dpi(ctx, 4); bottomMargin = dpi(ctx, 4)
        }
    }

    fun badge(ctx: Context, s: String, color: Int) = text(ctx, s, 12f, Color.BLACK, true).apply {
        background = rounded(color, dp(ctx, 8f))
        setPadding(dpi(ctx, 8), dpi(ctx, 3), dpi(ctx, 8), dpi(ctx, 3))
    }

    fun switchRow(ctx: Context, label: String, value: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) =
        Switch(ctx).apply {
            text = label; isChecked = value; setTextColor(if (enabled) TEXT else DIM); isEnabled = enabled
            setOnCheckedChangeListener { _, b -> onChange(b) }
            setPadding(0, dpi(ctx, 6), 0, dpi(ctx, 6))
        }

    fun <T> spinnerRow(ctx: Context, label: String, items: List<T>, selected: Int, names: (T) -> String, onSelect: (T) -> Unit): View {
        val row = hbox(ctx)
        row.addView(text(ctx, label).apply { layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f) })
        val sp = Spinner(ctx)
        val ad = object : ArrayAdapter<String>(ctx, android.R.layout.simple_spinner_item, items.map(names)) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup) =
                (super.getView(position, convertView, parent) as TextView).apply { setTextColor(TEXT) }
        }
        ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        sp.adapter = ad
        sp.setSelection(selected.coerceIn(0, (items.size - 1).coerceAtLeast(0)), false)
        sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = onSelect(items[pos])
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        row.addView(sp)
        return row
    }

    fun seekRow(ctx: Context, label: String, min: Int, max: Int, value: Int, fmt: (Int) -> String, onChange: (Int) -> Unit): View {
        val box = vbox(ctx, 0)
        val t = text(ctx, "$label: ${fmt(value)}")
        val sb = SeekBar(ctx).apply {
            this.max = max - min; progress = value - min
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, fromUser: Boolean) {
                    t.text = "$label: ${fmt(p + min)}"; if (fromUser) onChange(p + min)
                }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
        }
        box.addView(t); box.addView(sb)
        return box
    }

    /** A feature line with an honest status badge. */
    fun statusRow(ctx: Context, label: String, status: Status, detail: String = ""): View {
        val row = hbox(ctx).apply { setPadding(0, dpi(ctx, 4), 0, dpi(ctx, 4)) }
        row.addView(badge(ctx, status.label, status.color).apply {
            layoutParams = LinearLayout.LayoutParams(dpi(ctx, 120), ViewGroup.LayoutParams.WRAP_CONTENT); gravity = Gravity.CENTER
        })
        val col = vbox(ctx, 0).apply {
            setPadding(dpi(ctx, 10), 0, 0, 0)
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        col.addView(text(ctx, label, 15f, TEXT, true))
        if (detail.isNotEmpty()) col.addView(text(ctx, detail, 13f, DIM))
        row.addView(col)
        return row
    }

    enum class Status(val label: String, val color: Int) {
        WORKS("Berfungsi", OK), PARTIAL("Parsial", PARTIAL), MISSING("Belum tersedia", NO), NEEDS("Perlu tindakan", PARTIAL)
    }

    fun prompt(act: Activity, title: String, initial: String, onOk: (String) -> Unit) {
        val e = EditText(act).apply { setText(initial); inputType = InputType.TYPE_CLASS_TEXT; setTextColor(TEXT) }
        AlertDialog.Builder(act).setTitle(title).setView(e)
            .setPositiveButton("OK") { _, _ -> onOk(e.text.toString().trim()) }
            .setNegativeButton("Batal", null).show()
    }

    fun info(act: Activity, title: String, msg: String) {
        AlertDialog.Builder(act).setTitle(title).setMessage(msg).setPositiveButton("OK", null).show()
    }

    fun header(act: Activity, title: String, vararg actions: Pair<String, () -> Unit>): View {
        val row = hbox(act).apply { setPadding(dpi(act, 12), dpi(act, 10), dpi(act, 12), dpi(act, 4)) }
        row.addView(text(act, title, 22f, TEXT, true).apply {
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        })
        actions.forEach { (label, fn) -> row.addView(button(act, label) { fn() }) }
        return row
    }
}
