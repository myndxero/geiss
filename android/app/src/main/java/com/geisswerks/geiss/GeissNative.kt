package com.geisswerks.geiss

object GeissNative {
    init {
        System.loadLibrary("geiss_native")
    }

    const val NUM_MODES = 25
    const val NUM_EFFECTS = 8
    const val NUM_WAVES = 6 // 0=Off, 1..6 waveforms
    const val NUM_PRESET_SLOTS = 100

    val EFFECT_NAMES = arrayOf(
        "Chasers",
        "Bar",
        "Dots",
        "Solar",
        "Grid",
        "Nuclide",
        "Shade",
        "Spectral"
    )

    val WAVEFORM_NAMES = arrayOf(
        "0: Off",
        "1: Classic Dots",
        "2: Connected Line",
        "3: Dual Stereo",
        "4: Spiral Orbit",
        "5: Circular Ring",
        "6: Radial Burst"
    )

    data class EngineStatus(
        val effects: BooleanArray = BooleanArray(NUM_EFFECTS),
        val currentMode: Int = 1,
        val currentWaveform: Int = 1,
        val isSpectrumMode: Boolean = false,
        val isModeLocked: Boolean = false,
        val isPaletteLocked: Boolean = false,
        val isSlideShift: Boolean = false,
        val volpos: Int = 10,
        val isSoundActive: Boolean = false,
        val colorDepthBits: Int = 8,
        val fps: Float = 0f,
        val currentVolume: Float = 0f
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (other !is EngineStatus) return false
            return effects.contentEquals(other.effects) &&
                currentMode == other.currentMode &&
                currentWaveform == other.currentWaveform &&
                isSpectrumMode == other.isSpectrumMode &&
                isModeLocked == other.isModeLocked &&
                isPaletteLocked == other.isPaletteLocked &&
                isSlideShift == other.isSlideShift &&
                volpos == other.volpos &&
                isSoundActive == other.isSoundActive &&
                colorDepthBits == other.colorDepthBits &&
                fps == other.fps &&
                currentVolume == other.currentVolume
        }

        override fun hashCode(): Int {
            var r = effects.contentHashCode()
            r = 31 * r + currentMode
            r = 31 * r + currentWaveform
            r = 31 * r + isSpectrumMode.hashCode()
            r = 31 * r + isModeLocked.hashCode()
            r = 31 * r + isPaletteLocked.hashCode()
            r = 31 * r + isSlideShift.hashCode()
            r = 31 * r + volpos
            return r
        }
    }

    @JvmStatic external fun nativeCreate(): Long
    @JvmStatic external fun nativeDestroy(handle: Long)

    @JvmStatic external fun nativeInit(
        handle: Long,
        width: Int,
        height: Int,
        colorDepthBits: Int,
        verticalSizePct: Int,
        framesTilAutoSwitch: Int,
        gamma: Int,
        solarPalFreq: Int,
        coarsePalFreq: Int,
        volpos: Int,
        useBeatDetection: Boolean,
        syncColorToSound: Boolean,
        suppressAllMsg: Boolean,
        disableSongTitlePopups: Boolean,
        randomSongTitleFreq: Int,
        songTooltipFrames: Int,
        slideShiftFreq: Int,
        slideShiftMinFrames: Int,
        showFps: Boolean,
        autoDemoWhenSilent: Boolean
    )

    @JvmStatic external fun nativeResize(handle: Long, width: Int, height: Int)

    @JvmStatic external fun nativeUpdateConfig(
        handle: Long,
        colorDepthBits: Int,
        verticalSizePct: Int,
        framesTilAutoSwitch: Int,
        gamma: Int,
        solarPalFreq: Int,
        coarsePalFreq: Int,
        volpos: Int,
        useBeatDetection: Boolean,
        syncColorToSound: Boolean,
        suppressAllMsg: Boolean,
        disableSongTitlePopups: Boolean,
        randomSongTitleFreq: Int,
        songTooltipFrames: Int,
        slideShiftFreq: Int,
        slideShiftMinFrames: Int,
        showFps: Boolean,
        autoDemoWhenSilent: Boolean
    )

    @JvmStatic external fun nativeFeedWaveAndSpectrum8(
        handle: Long,
        waveL: ByteArray?,
        waveR: ByteArray?,
        waveLen: Int,
        specL: ByteArray?,
        specR: ByteArray?,
        specLen: Int,
        active: Boolean
    )

    @JvmStatic external fun nativeFeedPcm16Stereo(
        handle: Long,
        pcm: ShortArray,
        numFrames: Int,
        active: Boolean
    )

    @JvmStatic external fun nativeNextMap(handle: Long)
    @JvmStatic external fun nativeSelectMap(handle: Long, modeId: Int)
    @JvmStatic external fun nativeRandomizePalette(handle: Long)
    @JvmStatic external fun nativeToggleLockMode(handle: Long)
    @JvmStatic external fun nativeToggleLockPalette(handle: Long)
    @JvmStatic external fun nativeCycleWaveform(handle: Long)
    @JvmStatic external fun nativeSetWaveform(handle: Long, wf: Int)
    @JvmStatic external fun nativeToggleSpectrumMode(handle: Long)
    @JvmStatic external fun nativeSetSpectrumMode(handle: Long, enabled: Boolean)
    @JvmStatic external fun nativeToggleEffect(handle: Long, effectIdx: Int)
    @JvmStatic external fun nativeSetEffect(handle: Long, effectIdx: Int, enabled: Boolean)
    @JvmStatic external fun nativeToggleSlideShift(handle: Long)
    @JvmStatic external fun nativeAdjustVolumeScale(handle: Long, delta: Int)
    @JvmStatic external fun nativeSetVolumePos(handle: Long, pos: Int)
    @JvmStatic external fun nativeRefreshEdges(handle: Long)
    @JvmStatic external fun nativeTriggerTouchBurst(handle: Long, normX: Float, normY: Float, strength: Float)
    @JvmStatic external fun nativeSetLauncherOffset(handle: Long, offset: Float)
    @JvmStatic external fun nativeSetModeRating(handle: Long, modeId: Int, stars: Int)
    @JvmStatic external fun nativeSetAllModeRatings(handle: Long, ratings: IntArray)

    @JvmStatic external fun nativeExportPreset(
        handle: Long,
        name: String,
        outFloats: FloatArray,
        outInts: IntArray,
        outBools: BooleanArray
    )

    @JvmStatic external fun nativeLoadPreset(
        handle: Long,
        name: String,
        inFloats: FloatArray,
        inInts: IntArray,
        inBools: BooleanArray
    )

    @JvmStatic external fun nativeSetSongTitleText(handle: Long, title: String, isCustom: Boolean)
    @JvmStatic external fun nativeSetOverlayTextBitmap(handle: Long, rgba: ByteArray?, w: Int, h: Int, durationFrames: Int)
    @JvmStatic external fun nativeTriggerSongTitlePopup(handle: Long)

    @JvmStatic external fun nativeGetStatus(handle: Long, outInts: IntArray, outFloats: FloatArray)
    @JvmStatic external fun nativeGetModeName(modeId: Int): String

    // OpenGL ES 2.0 Renderer
    @JvmStatic external fun nativeGlCreate(): Long
    @JvmStatic external fun nativeGlDestroy(glHandle: Long)
    @JvmStatic external fun nativeGlOnSurfaceCreated(glHandle: Long)
    @JvmStatic external fun nativeGlOnSurfaceChanged(glHandle: Long, w: Int, h: Int)
    @JvmStatic external fun nativeGlDrawFrame(
        glHandle: Long,
        engineHandle: Long,
        dtSeconds: Float,
        linearFilter: Boolean,
        parallaxAmount: Float,
        hdrEnabled: Boolean,
        hdrIntensity: Float,
        vibrancy: Float,
        contrast: Float
    )

    fun readStatus(handle: Long): EngineStatus {
        val ints = IntArray(17)
        val floats = FloatArray(2)
        nativeGetStatus(handle, ints, floats)
        val eff = BooleanArray(NUM_EFFECTS) { i -> ints[i] != 0 }
        return EngineStatus(
            effects = eff,
            currentMode = ints[8],
            currentWaveform = ints[9],
            isSpectrumMode = ints[10] != 0,
            isModeLocked = ints[11] != 0,
            isPaletteLocked = ints[12] != 0,
            isSlideShift = ints[13] != 0,
            volpos = ints[14],
            isSoundActive = ints[15] != 0,
            colorDepthBits = ints[16],
            fps = floats[0],
            currentVolume = floats[1]
        )
    }
}
