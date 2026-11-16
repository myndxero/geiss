package com.geisswerks.geiss

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

enum class AudioSourceMode(val key: String, val label: String, val description: String) {
    OUTPUT_MIX(
        "OUTPUT_MIX",
        "Global Output Mix",
        "Silently reacts to music playing in any app (uses Android Visualizer)"
    ),
    PLAYBACK_CAPTURE(
        "PLAYBACK_CAPTURE",
        "System Audio Capture (Android 10+)",
        "True stereo 16-bit PCM & FFT from media playback (requires Cast audio prompt)"
    ),
    MICROPHONE(
        "MICROPHONE",
        "Microphone (Ambient)",
        "Classic Geiss audio input reacting to external speakers & room sound"
    );

    companion object {
        fun fromKey(key: String?): AudioSourceMode =
            entries.firstOrNull { it.key == key } ?: OUTPUT_MIX
    }
}

enum class RenderResolution(val key: String, val shortEdgePx: Int, val label: String) {
    LOW("LOW", 320, "Retro 320p (Max Battery)"),
    MEDIUM("MEDIUM", 480, "Balanced 480p (Recommended)"),
    HIGH("HIGH", 720, "Crisp 720p"),
    FULL_HD("FULL_HD", 1080, "Full HD 1080p"),
    NATIVE("NATIVE", Int.MAX_VALUE, "Native Resolution (Full Screen Pixels)");

    companion object {
        fun fromKey(key: String?): RenderResolution =
            entries.firstOrNull { it.key == key } ?: MEDIUM
    }
}

enum class ColorVibrancyPreset(
    val key: String,
    val label: String,
    val vibrancy: Float,
    val contrast: Float
) {
    CLASSIC("CLASSIC", "Classic 1998 (Balanced)", 1.0f, 1.0f),
    VIVID("VIVID", "Vivid OLED (Rich Colors & Deep Blacks)", 1.38f, 1.12f),
    HYPER_NEON("HYPER_NEON", "Hyper Neon (Maximum Punch)", 1.78f, 1.24f),
    PASTEL("PASTEL", "Pastel Dream (Soft Glow)", 0.72f, 0.96f),
    CUSTOM("CUSTOM", "Custom Vibrancy & Contrast", 1.38f, 1.12f);

    companion object {
        fun fromKey(key: String?): ColorVibrancyPreset =
            entries.firstOrNull { it.key == key } ?: VIVID
    }
}

enum class DoubleTapAction(val key: String, val label: String) {
    ACTION_WHEEL("ACTION_WHEEL", "Action Wheel (All Tap Options)"),
    NEXT_MAP("NEXT_MAP", "Next Motion Map"),
    RANDOM_PALETTE("RANDOM_PALETTE", "Randomize Palette"),
    SONG_POPUP("SONG_POPUP", "Show Song Title / Message"),
    TOGGLE_LOCK("TOGGLE_LOCK", "Toggle Lock Map"),
    NONE("NONE", "Disabled");

    companion object {
        fun fromKey(key: String?): DoubleTapAction =
            entries.firstOrNull { it.key == key } ?: NEXT_MAP
    }
}

data class GeissConfigSnapshot(
    val colorDepthBits: Int = 8,
    val verticalSizePct: Int = 100,
    val framesTilAutoSwitch: Int = 550,
    val gamma: Int = 10,
    val solarPalFreq: Int = 1,
    val coarsePalFreq: Int = 1,
    val volpos: Int = 10,
    val useBeatDetection: Boolean = true,
    val syncColorToSound: Boolean = true,
    val suppressAllMsg: Boolean = false,
    val disableSongTitlePopups: Boolean = false,
    val randomSongTitleFreq: Int = 3,
    val songTooltipFrames: Int = 45,
    val slideShiftFreq: Int = 25,
    val slideShiftMinFrames: Int = 6,
    val showFps: Boolean = false,
    val autoDemoWhenSilent: Boolean = true,
    val audioSourceMode: AudioSourceMode = AudioSourceMode.OUTPUT_MIX,
    val renderResolution: RenderResolution = RenderResolution.MEDIUM,
    val targetFps: Int = 60,
    val linearFilter: Boolean = true,
    val parallaxAmount: Float = 0.08f,
    val touchBurstEnabled: Boolean = true,
    val doubleTapAction: DoubleTapAction = DoubleTapAction.NEXT_MAP,
    val singleTapTriggersAction: Boolean = false,
    val hdrEnabled: Boolean = true,
    val hdrIntensity: Float = 1.15f,
    val colorVibrancyPreset: ColorVibrancyPreset = ColorVibrancyPreset.VIVID,
    val vibrancy: Float = 1.38f,
    val contrast: Float = 1.12f
)

data class PresetState(
    val name: String,
    val valid: Boolean = true,
    val xCenter: Float = 0.5f,
    val yCenter: Float = 0.5f,
    val mode: Int = 1,
    val waveform: Int = 1,
    val damping: Float = 1.0f,
    val magic: Int = 256,
    val shift: Boolean = false,
    val gamma: Int = 10,
    val volpos: Int = 10,
    val t1: Float = 0.01f,
    val t2: Float = 0.01f,
    val s1: Float = 0.96f,
    val s2: Float = 0.96f,
    val f1: Float = 0.95f,
    val f2: Float = 0.001f,
    val f3: Float = 0.05f,
    val f4: Float = 0.05f,
    val effects: IntArray = IntArray(GeissNative.NUM_EFFECTS) { -1 },
    val spectrum: Boolean = false,
    val loBand: Int = -1,
    val hiBand: Int = -1,
    val bFxPalette: Boolean = false,
    val iFxPaletteNum: Int = 0,
    val c1: Int = 1,
    val c2: Int = 2,
    val c3: Int = 3
) {
    fun toJson(): String {
        val obj = JSONObject()
        obj.put("name", name)
        obj.put("valid", valid)
        obj.put("xCenter", xCenter.toDouble())
        obj.put("yCenter", yCenter.toDouble())
        obj.put("mode", mode)
        obj.put("waveform", waveform)
        obj.put("damping", damping.toDouble())
        obj.put("magic", magic)
        obj.put("shift", shift)
        obj.put("gamma", gamma)
        obj.put("volpos", volpos)
        obj.put("t1", t1.toDouble())
        obj.put("t2", t2.toDouble())
        obj.put("s1", s1.toDouble())
        obj.put("s2", s2.toDouble())
        obj.put("f1", f1.toDouble())
        obj.put("f2", f2.toDouble())
        obj.put("f3", f3.toDouble())
        obj.put("f4", f4.toDouble())
        val effArr = JSONArray()
        effects.forEach { effArr.put(it) }
        obj.put("effects", effArr)
        obj.put("spectrum", spectrum)
        obj.put("loBand", loBand)
        obj.put("hiBand", hiBand)
        obj.put("bFxPalette", bFxPalette)
        obj.put("iFxPaletteNum", iFxPaletteNum)
        obj.put("c1", c1)
        obj.put("c2", c2)
        obj.put("c3", c3)
        return obj.toString()
    }

    fun toNativeArrays(): Triple<FloatArray, IntArray, BooleanArray> {
        val floats = floatArrayOf(xCenter, yCenter, damping, t1, t2, s1, s2, f1, f2, f3, f4)
        val ints = IntArray(19)
        ints[0] = mode
        ints[1] = waveform
        ints[2] = magic
        ints[3] = gamma
        ints[4] = volpos
        ints[5] = loBand
        ints[6] = hiBand
        ints[7] = iFxPaletteNum
        ints[8] = c1
        ints[9] = c2
        ints[10] = c3
        for (i in 0 until GeissNative.NUM_EFFECTS) {
            ints[11 + i] = effects.getOrElse(i) { -1 }
        }
        val bools = booleanArrayOf(valid, shift, spectrum, bFxPalette)
        return Triple(floats, ints, bools)
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is PresetState) return false
        return name == other.name &&
            valid == other.valid &&
            mode == other.mode &&
            waveform == other.waveform &&
            magic == other.magic &&
            shift == other.shift &&
            gamma == other.gamma &&
            volpos == other.volpos &&
            effects.contentEquals(other.effects) &&
            spectrum == other.spectrum &&
            loBand == other.loBand &&
            hiBand == other.hiBand &&
            bFxPalette == other.bFxPalette &&
            iFxPaletteNum == other.iFxPaletteNum &&
            c1 == other.c1 &&
            c2 == other.c2 &&
            c3 == other.c3
    }

    override fun hashCode(): Int {
        var result = name.hashCode()
        result = 31 * result + mode
        result = 31 * result + effects.contentHashCode()
        return result
    }

    companion object {
        fun fromJson(json: String): PresetState? {
            return try {
                val obj = JSONObject(json)
                val effArr = obj.optJSONArray("effects")
                val effects = IntArray(GeissNative.NUM_EFFECTS) { i ->
                    effArr?.optInt(i, -1) ?: -1
                }
                PresetState(
                    name = obj.optString("name", "Preset"),
                    valid = obj.optBoolean("valid", true),
                    xCenter = obj.optDouble("xCenter", 0.5).toFloat(),
                    yCenter = obj.optDouble("yCenter", 0.5).toFloat(),
                    mode = obj.optInt("mode", 1),
                    waveform = obj.optInt("waveform", 1),
                    damping = obj.optDouble("damping", 1.0).toFloat(),
                    magic = obj.optInt("magic", 256),
                    shift = obj.optBoolean("shift", false),
                    gamma = obj.optInt("gamma", 10),
                    volpos = obj.optInt("volpos", 10),
                    t1 = obj.optDouble("t1", 0.01).toFloat(),
                    t2 = obj.optDouble("t2", 0.01).toFloat(),
                    s1 = obj.optDouble("s1", 0.96).toFloat(),
                    s2 = obj.optDouble("s2", 0.96).toFloat(),
                    f1 = obj.optDouble("f1", 0.95).toFloat(),
                    f2 = obj.optDouble("f2", 0.001).toFloat(),
                    f3 = obj.optDouble("f3", 0.05).toFloat(),
                    f4 = obj.optDouble("f4", 0.05).toFloat(),
                    effects = effects,
                    spectrum = obj.optBoolean("spectrum", false),
                    loBand = obj.optInt("loBand", -1),
                    hiBand = obj.optInt("hiBand", -1),
                    bFxPalette = obj.optBoolean("bFxPalette", false),
                    iFxPaletteNum = obj.optInt("iFxPaletteNum", 0),
                    c1 = obj.optInt("c1", 1),
                    c2 = obj.optInt("c2", 2),
                    c3 = obj.optInt("c3", 3)
                )
            } catch (_: Exception) {
                null
            }
        }

        fun fromNativeArrays(name: String, floats: FloatArray, ints: IntArray, bools: BooleanArray): PresetState {
            val eff = IntArray(GeissNative.NUM_EFFECTS) { i -> ints[11 + i] }
            return PresetState(
                name = name,
                valid = bools[0],
                xCenter = floats[0],
                yCenter = floats[1],
                damping = floats[2],
                t1 = floats[3],
                t2 = floats[4],
                s1 = floats[5],
                s2 = floats[6],
                f1 = floats[7],
                f2 = floats[8],
                f3 = floats[9],
                f4 = floats[10],
                mode = ints[0],
                waveform = ints[1],
                magic = ints[2],
                gamma = ints[3],
                volpos = ints[4],
                loBand = ints[5],
                hiBand = ints[6],
                iFxPaletteNum = ints[7],
                c1 = ints[8],
                c2 = ints[9],
                c3 = ints[10],
                effects = eff,
                shift = bools[1],
                spectrum = bools[2],
                bFxPalette = bools[3]
            )
        }
    }
}

class GeissPreferences(context: Context) {
    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    init {
        seedDefaultsIfFirstRun()
    }

    fun registerListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs.registerOnSharedPreferenceChangeListener(listener)
    }

    fun unregisterListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs.unregisterOnSharedPreferenceChangeListener(listener)
    }

    fun getSnapshot(): GeissConfigSnapshot {
        return GeissConfigSnapshot(
            colorDepthBits = prefs.getInt(KEY_COLOR_DEPTH, 8),
            verticalSizePct = prefs.getInt(KEY_VERTICAL_SIZE, 100),
            framesTilAutoSwitch = prefs.getInt(KEY_AUTO_SWITCH_FRAMES, 550),
            gamma = prefs.getInt(KEY_GAMMA, 10),
            solarPalFreq = prefs.getInt(KEY_SOLAR_PAL_FREQ, 1),
            coarsePalFreq = prefs.getInt(KEY_COARSE_PAL_FREQ, 1),
            volpos = prefs.getInt(KEY_VOLPOS, 10),
            useBeatDetection = prefs.getBoolean(KEY_BEAT_DETECTION, true),
            syncColorToSound = prefs.getBoolean(KEY_SYNC_COLOR_SOUND, true),
            suppressAllMsg = prefs.getBoolean(KEY_SUPPRESS_ALL_MSG, false),
            disableSongTitlePopups = prefs.getBoolean(KEY_DISABLE_SONG_POPUPS, false),
            randomSongTitleFreq = prefs.getInt(KEY_RANDOM_SONG_FREQ, 3),
            songTooltipFrames = prefs.getInt(KEY_TOOLTIP_FRAMES, 45),
            slideShiftFreq = prefs.getInt(KEY_SLIDE_SHIFT_FREQ, 25),
            slideShiftMinFrames = prefs.getInt(KEY_SLIDE_SHIFT_MIN, 6),
            showFps = prefs.getBoolean(KEY_SHOW_FPS, false),
            autoDemoWhenSilent = prefs.getBoolean(KEY_AUTO_DEMO_SILENT, true),
            audioSourceMode = AudioSourceMode.fromKey(prefs.getString(KEY_AUDIO_SOURCE, AudioSourceMode.OUTPUT_MIX.key)),
            renderResolution = RenderResolution.fromKey(prefs.getString(KEY_RENDER_RES, RenderResolution.MEDIUM.key)),
            targetFps = prefs.getInt(KEY_TARGET_FPS, 60),
            linearFilter = prefs.getBoolean(KEY_LINEAR_FILTER, true),
            parallaxAmount = prefs.getFloat(KEY_PARALLAX, 0.08f),
            touchBurstEnabled = prefs.getBoolean(KEY_TOUCH_BURST, true),
            doubleTapAction = DoubleTapAction.fromKey(prefs.getString(KEY_DOUBLE_TAP, DoubleTapAction.NEXT_MAP.key)),
            singleTapTriggersAction = prefs.getBoolean(KEY_SINGLE_TAP_ACTION, false),
            hdrEnabled = prefs.getBoolean(KEY_HDR_ENABLED, true),
            hdrIntensity = prefs.getFloat(KEY_HDR_INTENSITY, 1.15f),
            colorVibrancyPreset = ColorVibrancyPreset.fromKey(prefs.getString(KEY_VIBRANCY_PRESET, ColorVibrancyPreset.VIVID.key)),
            vibrancy = prefs.getFloat(KEY_VIBRANCY, ColorVibrancyPreset.VIVID.vibrancy),
            contrast = prefs.getFloat(KEY_CONTRAST, ColorVibrancyPreset.VIVID.contrast)
        )
    }

    fun update(block: SharedPreferences.Editor.() -> Unit) {
        val editor = prefs.edit()
        editor.block()
        editor.apply()
    }

    fun getModeRatings(): IntArray {
        val ratings = IntArray(GeissNative.NUM_MODES + 1)
        for (i in 1..GeissNative.NUM_MODES) {
            ratings[i] = prefs.getInt("${KEY_MODE_RATING_PREFIX}$i", 3).coerceIn(0, 5)
        }
        return ratings
    }

    fun setModeRating(modeId: Int, stars: Int) {
        if (modeId in 1..GeissNative.NUM_MODES) {
            prefs.edit().putInt("${KEY_MODE_RATING_PREFIX}$modeId", stars.coerceIn(0, 5)).apply()
        }
    }

    fun getPreset(slot: Int): PresetState? {
        if (slot !in 0 until GeissNative.NUM_PRESET_SLOTS) return null
        val json = prefs.getString("${KEY_PRESET_PREFIX}$slot", null) ?: return null
        return PresetState.fromJson(json)
    }

    fun savePreset(slot: Int, preset: PresetState) {
        if (slot !in 0 until GeissNative.NUM_PRESET_SLOTS) return
        prefs.edit().putString("${KEY_PRESET_PREFIX}$slot", preset.toJson()).apply()
    }

    fun deletePreset(slot: Int) {
        if (slot !in 0 until GeissNative.NUM_PRESET_SLOTS) return
        prefs.edit().remove("${KEY_PRESET_PREFIX}$slot").apply()
    }

    fun getCustomMessage(slot: Int): String {
        if (slot !in 0 until GeissNative.NUM_PRESET_SLOTS) return ""
        return prefs.getString("${KEY_MSG_PREFIX}$slot", "") ?: ""
    }

    fun setCustomMessage(slot: Int, message: String) {
        if (slot !in 0 until GeissNative.NUM_PRESET_SLOTS) return
        val trimmed = message.trim()
        prefs.edit().apply {
            if (trimmed.isEmpty()) remove("${KEY_MSG_PREFIX}$slot")
            else putString("${KEY_MSG_PREFIX}$slot", trimmed)
        }.apply()
    }

    fun getAllNonEmptyCustomMessages(): List<Pair<Int, String>> {
        val out = mutableListOf<Pair<Int, String>>()
        for (i in 0 until GeissNative.NUM_PRESET_SLOTS) {
            val msg = getCustomMessage(i)
            if (msg.isNotEmpty()) out.add(i to msg)
        }
        return out
    }

    private fun seedDefaultsIfFirstRun() {
        if (prefs.getBoolean(KEY_SEEDED, false)) return
        prefs.edit().apply {
            val starters = listOf(
                0 to PresetState(
                    name = "00 Classic Vortex",
                    mode = 1,
                    waveform = 2,
                    magic = 254,
                    t1 = 0.021f,
                    s1 = 0.968f,
                    effects = intArrayOf(1, -1, -1, 1, -1, -1, -1, -1),
                    c1 = 1, c2 = 4, c3 = 7
                ),
                1 to PresetState(
                    name = "01 Super-Perspective",
                    mode = 5,
                    waveform = 3,
                    magic = 256,
                    f1 = 0.955f, f2 = 0.004f, f3 = 0.04f, f4 = 0.04f,
                    effects = intArrayOf(-1, 1, -1, 1, -1, -1, 1, -1),
                    c1 = 3, c2 = 6, c3 = 11
                ),
                2 to PresetState(
                    name = "02 Phonic Rings Spectrum",
                    mode = 22,
                    waveform = 5,
                    magic = 255,
                    spectrum = true,
                    effects = intArrayOf(1, -1, 1, -1, -1, -1, -1, 1),
                    c1 = 2, c2 = 8, c3 = 14
                ),
                3 to PresetState(
                    name = "03 Gravity Vortices",
                    mode = 6,
                    waveform = 4,
                    magic = 254,
                    effects = intArrayOf(-1, -1, -1, 1, -1, 1, -1, -1),
                    bFxPalette = true,
                    iFxPaletteNum = 1
                ),
                4 to PresetState(
                    name = "04 Hyper Zoom Tunnel",
                    mode = 25,
                    waveform = 6,
                    magic = 256,
                    effects = intArrayOf(1, -1, 1, 1, -1, -1, -1, -1),
                    c1 = 5, c2 = 9, c3 = 13
                )
            )
            for ((slot, p) in starters) {
                putString("${KEY_PRESET_PREFIX}$slot", p.toJson())
            }
            putString("${KEY_MSG_PREFIX}0", "GEISS LIVE")
            putBoolean(KEY_SEEDED, true)
        }.apply()
    }

    companion object {
        const val PREFS_NAME = "geiss_prefs"
        const val KEY_SEEDED = "seeded_v1"

        const val KEY_COLOR_DEPTH = "color_depth_bits"
        const val KEY_VERTICAL_SIZE = "vertical_size_pct"
        const val KEY_AUTO_SWITCH_FRAMES = "frames_til_auto_switch"
        const val KEY_GAMMA = "gamma"
        const val KEY_SOLAR_PAL_FREQ = "solar_pal_freq"
        const val KEY_COARSE_PAL_FREQ = "coarse_pal_freq"
        const val KEY_VOLPOS = "volpos"
        const val KEY_BEAT_DETECTION = "use_beat_detection"
        const val KEY_SYNC_COLOR_SOUND = "sync_color_to_sound"
        const val KEY_SUPPRESS_ALL_MSG = "suppress_all_msg"
        const val KEY_DISABLE_SONG_POPUPS = "disable_song_popups"
        const val KEY_RANDOM_SONG_FREQ = "random_song_freq"
        const val KEY_TOOLTIP_FRAMES = "tooltip_frames"
        const val KEY_SLIDE_SHIFT_FREQ = "slide_shift_freq"
        const val KEY_SLIDE_SHIFT_MIN = "slide_shift_min"
        const val KEY_SHOW_FPS = "show_fps"
        const val KEY_AUTO_DEMO_SILENT = "auto_demo_silent"

        const val KEY_AUDIO_SOURCE = "audio_source_mode"
        const val KEY_RENDER_RES = "render_resolution"
        const val KEY_TARGET_FPS = "target_fps"
        const val KEY_LINEAR_FILTER = "linear_filter"
        const val KEY_PARALLAX = "parallax_amount"
        const val KEY_TOUCH_BURST = "touch_burst"
        const val KEY_DOUBLE_TAP = "double_tap_action"
        const val KEY_SINGLE_TAP_ACTION = "single_tap_action"
        const val KEY_HDR_ENABLED = "hdr_enabled"
        const val KEY_HDR_INTENSITY = "hdr_intensity"
        const val KEY_VIBRANCY_PRESET = "vibrancy_preset"
        const val KEY_VIBRANCY = "vibrancy"
        const val KEY_CONTRAST = "contrast"

        const val KEY_MODE_RATING_PREFIX = "mode_rating_"
        const val KEY_PRESET_PREFIX = "preset_slot_"
        const val KEY_MSG_PREFIX = "custom_msg_"

        fun computeRenderSize(surfaceW: Int, surfaceH: Int, res: RenderResolution): Pair<Int, Int> {
            val w = max(160, surfaceW)
            val h = max(160, surfaceH)
            val shortEdge = min(w, h)
            val targetShort = min(res.shortEdgePx, shortEdge)
            val scale = targetShort.toFloat() / shortEdge.toFloat()
            var rw = max(160, (w * scale).roundToInt())
            var rh = max(160, (h * scale).roundToInt())
            // Align to even pixel counts for clean texture & center coords
            rw = rw and 1.inv()
            rh = rh and 1.inv()
            return rw to rh
        }
    }
}
