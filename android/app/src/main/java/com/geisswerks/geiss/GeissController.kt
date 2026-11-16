package com.geisswerks.geiss

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArraySet
import kotlin.math.max
import kotlin.math.min

class GeissController private constructor(context: Context) {

    interface EngineSurface {
        fun applyConfig(snapshot: GeissConfigSnapshot)
        fun applyModeRatings(ratings: IntArray)
        fun withEngine(block: (handle: Long, renderW: Int, renderH: Int) -> Unit)
        fun getLatestStatus(): GeissNative.EngineStatus?
    }

    val appContext: Context = context.applicationContext
    val prefs = GeissPreferences(appContext)

    private val surfaces = CopyOnWriteArraySet<EngineSurface>()
    private val uiListeners = CopyOnWriteArraySet<(GeissNative.EngineStatus) -> Unit>()
    private val songListeners = CopyOnWriteArraySet<(String) -> Unit>()

    @Volatile
    var currentSongTitle: String = ""
        private set

    private val prefListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ ->
        val snap = prefs.getSnapshot()
        GeissAudioManager.setAudioSource(snap.audioSourceMode)
        val ratings = prefs.getModeRatings()
        for (s in surfaces) {
            s.applyConfig(snap)
            s.applyModeRatings(ratings)
        }
    }

    init {
        GeissAudioManager.init(appContext)
        GeissAudioManager.setAudioSource(prefs.getSnapshot().audioSourceMode)
        prefs.registerListener(prefListener)
    }

    fun registerSurface(surface: EngineSurface) {
        surfaces.add(surface)
        surface.applyConfig(prefs.getSnapshot())
        surface.applyModeRatings(prefs.getModeRatings())
        if (currentSongTitle.isNotEmpty() && !prefs.getSnapshot().disableSongTitlePopups) {
            surface.withEngine { handle, rw, rh ->
                GeissNative.nativeSetSongTitleText(handle, currentSongTitle, false)
                stampBitmapText(handle, currentSongTitle, rw, rh, prefs.getSnapshot().songTooltipFrames)
            }
        }
    }

    fun unregisterSurface(surface: EngineSurface) {
        surfaces.remove(surface)
    }

    fun addUiStatusListener(listener: (GeissNative.EngineStatus) -> Unit) {
        uiListeners.add(listener)
    }

    fun removeUiStatusListener(listener: (GeissNative.EngineStatus) -> Unit) {
        uiListeners.remove(listener)
    }

    fun addSongListener(listener: (String) -> Unit) {
        songListeners.add(listener)
        listener(currentSongTitle)
    }

    fun removeSongListener(listener: (String) -> Unit) {
        songListeners.remove(listener)
    }

    fun notifyStatusSample(status: GeissNative.EngineStatus) {
        for (l in uiListeners) {
            l(status)
        }
    }

    fun primaryStatus(): GeissNative.EngineStatus? {
        for (s in surfaces) {
            s.getLatestStatus()?.let { return it }
        }
        return null
    }

    fun forEachEngine(block: (handle: Long) -> Unit) {
        for (s in surfaces) {
            s.withEngine { handle, _, _ -> block(handle) }
        }
    }

    fun nextMap() {
        forEachEngine { GeissNative.nativeNextMap(it) }
    }

    fun selectMap(modeId: Int) {
        forEachEngine { GeissNative.nativeSelectMap(it, modeId) }
        val snap = prefs.getSnapshot()
        if (!snap.suppressAllMsg) {
            showOverlayMessage("${modeId.toString().padStart(2, '0')}: ${GeissNative.nativeGetModeName(modeId)}")
        }
    }

    fun randomizePalette() {
        forEachEngine { GeissNative.nativeRandomizePalette(it) }
    }

    fun toggleLockMode() {
        forEachEngine { GeissNative.nativeToggleLockMode(it) }
    }

    fun toggleLockPalette() {
        forEachEngine { GeissNative.nativeToggleLockPalette(it) }
    }

    fun cycleWaveform() {
        forEachEngine { GeissNative.nativeCycleWaveform(it) }
    }

    fun setWaveform(wf: Int) {
        forEachEngine { GeissNative.nativeSetWaveform(it, wf) }
    }

    fun toggleSpectrumMode() {
        forEachEngine { GeissNative.nativeToggleSpectrumMode(it) }
    }

    fun setSpectrumMode(enabled: Boolean) {
        forEachEngine { GeissNative.nativeSetSpectrumMode(it, enabled) }
    }

    fun toggleEffect(effectIdx: Int) {
        forEachEngine { GeissNative.nativeToggleEffect(it, effectIdx) }
    }

    fun toggleSlideShift() {
        forEachEngine { GeissNative.nativeToggleSlideShift(it) }
    }

    fun adjustVolumeScale(delta: Int) {
        var newPos = prefs.getSnapshot().volpos + delta
        newPos = newPos.coerceIn(0, 20)
        setVolumePos(newPos)
    }

    fun setVolumePos(pos: Int) {
        val clamped = pos.coerceIn(0, 20)
        prefs.update { putInt(GeissPreferences.KEY_VOLPOS, clamped) }
        forEachEngine { GeissNative.nativeSetVolumePos(it, clamped) }
    }

    fun refreshEdges() {
        forEachEngine { GeissNative.nativeRefreshEdges(it) }
    }

    fun toggleHdr(): Boolean {
        val next = !prefs.getSnapshot().hdrEnabled
        prefs.update { putBoolean(GeissPreferences.KEY_HDR_ENABLED, next) }
        if (!prefs.getSnapshot().suppressAllMsg) {
            showOverlayMessage(if (next) "HDR Bloom ON" else "HDR Bloom OFF")
        }
        return next
    }

    fun cycleVibrancyPreset(): ColorVibrancyPreset {
        val cycle = listOf(
            ColorVibrancyPreset.CLASSIC,
            ColorVibrancyPreset.VIVID,
            ColorVibrancyPreset.HYPER_NEON,
            ColorVibrancyPreset.PASTEL
        )
        val current = prefs.getSnapshot().colorVibrancyPreset
        val idx = cycle.indexOf(current)
        val next = cycle[(idx + 1) % cycle.size]
        prefs.update {
            putString(GeissPreferences.KEY_VIBRANCY_PRESET, next.key)
            putFloat(GeissPreferences.KEY_VIBRANCY, next.vibrancy)
            putFloat(GeissPreferences.KEY_CONTRAST, next.contrast)
        }
        if (!prefs.getSnapshot().suppressAllMsg) {
            showOverlayMessage(next.label)
        }
        return next
    }

    fun executeWheelAction(action: GeissActionWheelView.WheelAction): String? {
        return when (action) {
            GeissActionWheelView.WheelAction.NEXT_MAP -> {
                nextMap()
                null
            }
            GeissActionWheelView.WheelAction.RANDOM_PALETTE -> {
                randomizePalette()
                "Palette Randomized"
            }
            GeissActionWheelView.WheelAction.TOGGLE_LOCK_MAP -> {
                toggleLockMode()
                null
            }
            GeissActionWheelView.WheelAction.TOGGLE_LOCK_PAL -> {
                toggleLockPalette()
                null
            }
            GeissActionWheelView.WheelAction.SONG_POPUP -> {
                triggerSongTitlePopup()
                null
            }
            GeissActionWheelView.WheelAction.CYCLE_WAVE -> {
                cycleWaveform()
                null
            }
            GeissActionWheelView.WheelAction.TOGGLE_SPECTRUM -> {
                toggleSpectrumMode()
                null
            }
            GeissActionWheelView.WheelAction.TOGGLE_SHIFT -> {
                toggleSlideShift()
                null
            }
            GeissActionWheelView.WheelAction.TOGGLE_HDR -> {
                val on = toggleHdr()
                if (on) "HDR Bloom ON" else "HDR Bloom OFF"
            }
            GeissActionWheelView.WheelAction.CYCLE_VIBRANCY -> {
                cycleVibrancyPreset().label
            }
            GeissActionWheelView.WheelAction.REFRESH_EDGES -> {
                refreshEdges()
                "Edges Refreshed"
            }
            GeissActionWheelView.WheelAction.OPEN_SETTINGS -> null
        }
    }

    fun exportPresetFromActive(slotName: String): PresetState? {
        for (s in surfaces) {
            var exported: PresetState? = null
            s.withEngine { handle, _, _ ->
                if (exported == null) {
                    val floats = FloatArray(11)
                    val ints = IntArray(19)
                    val bools = BooleanArray(4)
                    GeissNative.nativeExportPreset(handle, slotName, floats, ints, bools)
                    exported = PresetState.fromNativeArrays(slotName, floats, ints, bools)
                }
            }
            if (exported != null) return exported
        }
        return null
    }

    fun loadPreset(slot: Int): Boolean {
        val preset = prefs.getPreset(slot) ?: return false
        val (floats, ints, bools) = preset.toNativeArrays()
        forEachEngine { handle ->
            GeissNative.nativeLoadPreset(handle, preset.name, floats, ints, bools)
        }
        val snap = prefs.getSnapshot()
        if (!snap.suppressAllMsg) {
            showOverlayMessage("Preset ${slot.toString().padStart(2, '0')}: ${preset.name}")
        }
        return true
    }

    fun savePreset(slot: Int, customName: String): PresetState? {
        val label = customName.trim().ifEmpty { "Preset ${slot.toString().padStart(2, '0')}" }
        val exported = exportPresetFromActive(label) ?: return null
        prefs.savePreset(slot, exported)
        if (!prefs.getSnapshot().suppressAllMsg) {
            showOverlayMessage("Saved Preset ${slot.toString().padStart(2, '0')}")
        }
        return exported
    }

    fun onDetectedSongChanged(title: String) {
        if (title.isBlank() || title == currentSongTitle) return
        currentSongTitle = title
        for (l in songListeners) {
            l(title)
        }
        val snap = prefs.getSnapshot()
        if (snap.suppressAllMsg || snap.disableSongTitlePopups) return

        for (s in surfaces) {
            s.withEngine { handle, rw, rh ->
                GeissNative.nativeSetSongTitleText(handle, title, false)
                stampBitmapText(handle, title, rw, rh, snap.songTooltipFrames)
            }
        }
    }

    fun triggerSongTitlePopup() {
        val snap = prefs.getSnapshot()
        if (snap.suppressAllMsg) return
        val text = when {
            currentSongTitle.isNotBlank() -> currentSongTitle
            else -> {
                val msgs = prefs.getAllNonEmptyCustomMessages()
                if (msgs.isNotEmpty()) msgs.random().second else "GEISS LIVE"
            }
        }
        showOverlayMessage(text, isCustom = currentSongTitle.isBlank())
    }

    fun showCustomMessage(slot: Int) {
        val msg = prefs.getCustomMessage(slot)
        if (msg.isBlank()) return
        showOverlayMessage(msg, isCustom = true)
    }

    fun showOverlayMessage(text: String, isCustom: Boolean = false) {
        val snap = prefs.getSnapshot()
        if (snap.suppressAllMsg && !isCustom) return
        for (s in surfaces) {
            s.withEngine { handle, rw, rh ->
                GeissNative.nativeSetSongTitleText(handle, text, isCustom)
                stampBitmapText(handle, text, rw, rh, snap.songTooltipFrames)
            }
        }
    }

    private fun stampBitmapText(
        handle: Long,
        text: String,
        renderW: Int,
        renderH: Int,
        durationFrames: Int
    ) {
        val rendered = renderOverlayBitmap(text, renderW, renderH) ?: return
        val (bytes, w, h) = rendered
        GeissNative.nativeSetOverlayTextBitmap(handle, bytes, w, h, durationFrames)
    }

    companion object {
        @Volatile
        private var instance: GeissController? = null

        fun getInstance(context: Context): GeissController {
            return instance ?: synchronized(this) {
                instance ?: GeissController(context.applicationContext).also { instance = it }
            }
        }

        fun renderOverlayBitmap(text: String, renderW: Int, renderH: Int): Triple<ByteArray, Int, Int>? {
            if (text.isBlank() || renderW < 64 || renderH < 64) return null

            val maxTextWidth = max(48, (renderW * 0.82f).toInt())
            val fontSize = (min(renderW, renderH) * 0.062f).coerceIn(14f, 44f)

            val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                textSize = fontSize
                typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
                setShadowLayer(fontSize * 0.14f, 2f, 2f, Color.argb(220, 0, 0, 0))
            }

            val layout = StaticLayout.Builder
                .obtain(text, 0, text.length, paint, maxTextWidth)
                .setAlignment(Layout.Alignment.ALIGN_CENTER)
                .setMaxLines(3)
                .build()

            var maxLineW = 0f
            for (i in 0 until layout.lineCount) {
                maxLineW = max(maxLineW, layout.getLineWidth(i))
            }
            val bmpW = (maxLineW.toInt() + 16).coerceIn(16, renderW)
            val bmpH = (layout.height + 16).coerceIn(16, renderH / 2)

            val bitmap = Bitmap.createBitmap(bmpW, bmpH, Bitmap.Config.ARGB_8888)
            val canvas = Canvas(bitmap)
            canvas.save()
            val xShift = (bmpW - maxTextWidth) / 2f
            canvas.translate(xShift, 6f)
            layout.draw(canvas)
            canvas.restore()

            val byteBuf = ByteBuffer.allocate(bmpW * bmpH * 4)
            bitmap.copyPixelsToBuffer(byteBuf)
            bitmap.recycle()
            return Triple(byteBuf.array(), bmpW, bmpH)
        }
    }
}
