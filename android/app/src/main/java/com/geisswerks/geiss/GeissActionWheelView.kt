package com.geisswerks.geiss

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

class GeissActionWheelView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    enum class WheelAction(val label: String) {
        NEXT_MAP("Next\nMap"),
        RANDOM_PALETTE("Random\nPalette"),
        TOGGLE_LOCK_MAP("Lock\nMap"),
        TOGGLE_LOCK_PAL("Lock\nPalette"),
        SONG_POPUP("Song\nPopup"),
        CYCLE_WAVE("Cycle\nWave"),
        TOGGLE_SPECTRUM("Spec-\ntrum"),
        TOGGLE_SHIFT("Slide\nShift"),
        TOGGLE_HDR("HDR\nBloom"),
        CYCLE_VIBRANCY("Color\nVibrancy"),
        REFRESH_EDGES("Refresh\nEdges"),
        OPEN_SETTINGS("Settings")
    }

    var onActionSelected: ((WheelAction) -> Unit)? = null
    var onDismissRequested: (() -> Unit)? = null

    private var activeStates = BooleanArray(WheelAction.entries.size)
    private var hoveredIndex = -1
    private var centerX = 0f
    private var centerY = 0f
    private var outerRadius = 0f
    private var innerRadius = 0f

    private var customTapX = -1f
    private var customTapY = -1f

    private val backdropPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(135, 4, 6, 14)
        style = Paint.Style.FILL
    }
    private val slicePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(190, 74, 92, 150)
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }
    private val hubPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(242, 14, 19, 36)
        style = Paint.Style.FILL
    }
    private val hubRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(230, 0, 229, 255)
        style = Paint.Style.STROKE
        strokeWidth = 4f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }
    private val hubTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.argb(240, 0, 229, 255)
        textAlign = Paint.Align.CENTER
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
    }

    private val arcRect = RectF()

    fun setAnchor(x: Float, y: Float) {
        customTapX = x
        customTapY = y
        recomputeGeometry()
        invalidate()
    }

    fun updateActiveFlags(status: GeissNative.EngineStatus?, snap: GeissConfigSnapshot) {
        val flags = BooleanArray(WheelAction.entries.size)
        if (status != null) {
            flags[WheelAction.TOGGLE_LOCK_MAP.ordinal] = status.isModeLocked
            flags[WheelAction.TOGGLE_LOCK_PAL.ordinal] = status.isPaletteLocked
            flags[WheelAction.TOGGLE_SPECTRUM.ordinal] = status.isSpectrumMode
            flags[WheelAction.TOGGLE_SHIFT.ordinal] = status.isSlideShift
        }
        flags[WheelAction.TOGGLE_HDR.ordinal] = snap.hdrEnabled
        activeStates = flags
        invalidate()
    }

    fun showAnimated() {
        visibility = VISIBLE
        alpha = 0f
        scaleX = 0.84f
        scaleY = 0.84f
        animate()
            .alpha(1f)
            .scaleX(1f)
            .scaleY(1f)
            .setDuration(140L)
            .start()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        recomputeGeometry()
    }

    private fun recomputeGeometry() {
        if (width <= 0 || height <= 0) return
        val minDim = min(width, height).toFloat()
        outerRadius = (minDim * 0.42f).coerceIn(190f, 440f)
        innerRadius = outerRadius * 0.28f

        val margin = outerRadius + 24f
        centerX = if (customTapX > 0f) {
            customTapX.coerceIn(margin, (width - margin).coerceAtLeast(margin))
        } else {
            width * 0.5f
        }
        centerY = if (customTapY > 0f) {
            customTapY.coerceIn(margin, (height - margin).coerceAtLeast(margin))
        } else {
            height * 0.5f
        }

        arcRect.set(
            centerX - outerRadius,
            centerY - outerRadius,
            centerX + outerRadius,
            centerY + outerRadius
        )
        textPaint.textSize = (outerRadius * 0.088f).coerceIn(18f, 30f)
        hubTextPaint.textSize = (innerRadius * 0.46f).coerceIn(24f, 44f)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backdropPaint)

        val actions = WheelAction.entries
        val count = actions.size
        val sweep = 360f / count
        val midR = (outerRadius + innerRadius) * 0.53f

        for (i in 0 until count) {
            val startAngle = -90f + i * sweep - sweep * 0.5f
            val isHovered = (i == hoveredIndex)
            val isActive = activeStates.getOrElse(i) { false }

            slicePaint.color = when {
                isHovered -> Color.argb(235, 0, 195, 225)
                isActive -> Color.argb(225, 42, 74, 138)
                else -> Color.argb(218, 16, 22, 42)
            }
            canvas.drawArc(arcRect, startAngle, sweep, true, slicePaint)
            canvas.drawArc(arcRect, startAngle, sweep, true, strokePaint)

            val rad = (startAngle + sweep * 0.5f) * (PI / 180.0)
            val tx = centerX + (midR * cos(rad)).toFloat()
            val ty = centerY + (midR * sin(rad)).toFloat()

            textPaint.color = if (isHovered) Color.rgb(4, 19, 26)
            else if (isActive) Color.rgb(0, 229, 255)
            else Color.rgb(242, 246, 255)

            val lines = actions[i].label.split('\n')
            val lineH = textPaint.textSize * 1.15f
            val baseY = ty - (lines.size - 1) * lineH * 0.5f + textPaint.textSize * 0.34f
            for (li in lines.indices) {
                canvas.drawText(lines[li], tx, baseY + li * lineH, textPaint)
            }
        }

        // Center close hub
        canvas.drawCircle(centerX, centerY, innerRadius, hubPaint)
        canvas.drawCircle(centerX, centerY, innerRadius, hubRingPaint)
        canvas.drawText("✕", centerX, centerY + hubTextPaint.textSize * 0.35f, hubTextPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val dx = event.x - centerX
        val dy = event.y - centerY
        val dist = hypot(dx, dy)
        val count = WheelAction.entries.size
        val sweep = 360f / count

        val hitSlice = if (dist in innerRadius..outerRadius) {
            var deg = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble())).toFloat()
            deg = (deg + 90f + sweep * 0.5f + 360f) % 360f
            (deg / sweep).toInt().coerceIn(0, count - 1)
        } else {
            -1
        }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_MOVE -> {
                if (hoveredIndex != hitSlice) {
                    hoveredIndex = hitSlice
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                val chosen = hitSlice
                hoveredIndex = -1
                invalidate()
                if (chosen >= 0) {
                    onActionSelected?.invoke(WheelAction.entries[chosen])
                } else {
                    onDismissRequested?.invoke()
                }
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                hoveredIndex = -1
                invalidate()
                onDismissRequested?.invoke()
                return true
            }
        }
        return super.onTouchEvent(event)
    }
}
