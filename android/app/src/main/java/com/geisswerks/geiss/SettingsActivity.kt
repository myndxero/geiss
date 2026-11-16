package com.geisswerks.geiss

import android.Manifest
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.geisswerks.geiss.databinding.ActivitySettingsBinding
import com.google.android.material.card.MaterialCardView
import com.google.android.material.materialswitch.MaterialSwitch
import kotlin.math.roundToInt

class SettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivitySettingsBinding
    private lateinit var controller: GeissController
    private lateinit var prefs: GeissPreferences

    private val requestAudioPerm =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) {
                GeissAudioManager.refreshCapture()
            } else {
                Toast.makeText(this, "RECORD_AUDIO permission denied", Toast.LENGTH_SHORT).show()
            }
        }

    private val requestProjection =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { res ->
            if (res.resultCode == RESULT_OK && res.data != null) {
                GeissAudioCaptureService.startWithProjection(this, res.resultCode, res.data!!)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.toolbar.setNavigationOnClickListener { finish() }

        controller = GeissController.getInstance(this)
        prefs = controller.prefs

        buildSettingsUi()
    }

    private fun buildSettingsUi() {
        val snap = prefs.getSnapshot()
        val container = binding.settingsContainer
        container.removeAllViews()

        // 1. Audio Capture Section
        addSection(container, "Audio & Music Reactivity") { card ->
            addButton(
                card,
                "Audio Source: ${snap.audioSourceMode.label}",
                "Current: ${GeissAudioManager.status.message}"
            ) {
                ModesAndPresetsDialogs.showAudioSourceDialog(
                    this,
                    controller,
                    onRequestAudioPermission = { requestAudioPerm.launch(Manifest.permission.RECORD_AUDIO) },
                    onRequestPlaybackCapture = {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
                            requestProjection.launch(mgr.createScreenCaptureIntent())
                        }
                    }
                )
            }

            addSlider(
                card,
                title = "Audio Sensitivity (Volpos)",
                min = 0,
                max = 20,
                initial = snap.volpos,
                formatter = { "$it (scale ${String.format("%.2f", Math.pow(1.25, (it - 10).toDouble()))}x)" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_VOLPOS, v) }
            }

            addSwitch(
                card,
                title = "Beat Detection",
                subtitle = "Pulse palette & auto-advance modes on musical beats",
                initial = snap.useBeatDetection
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_BEAT_DETECTION, checked) }
            }

            addSwitch(
                card,
                title = "Auto-Demo When Silent",
                subtitle = "Show subtle ambient plasma wave when no music is playing",
                initial = snap.autoDemoWhenSilent
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_AUTO_DEMO_SILENT, checked) }
            }
        }

        // 2. Display & Live Wallpaper Performance
        addSection(container, "Display & Wallpaper Performance") { card ->
            addRadioChoices(
                card,
                title = "Internal Render Resolution",
                options = RenderResolution.entries.map { it.key to it.label },
                selectedKey = snap.renderResolution.key
            ) { key ->
                prefs.update { putString(GeissPreferences.KEY_RENDER_RES, key) }
            }

            addRadioChoices(
                card,
                title = "Target Frame Rate",
                options = listOf("30" to "30 FPS (Battery Saver)", "45" to "45 FPS", "60" to "60 FPS (Smooth)"),
                selectedKey = snap.targetFps.toString()
            ) { key ->
                prefs.update { putInt(GeissPreferences.KEY_TARGET_FPS, key.toIntOrNull() ?: 60) }
            }

            addRadioChoices(
                card,
                title = "Color Depth",
                options = listOf(
                    "8" to "8-bit Indexed Palette (Classic Geiss)",
                    "32" to "32-bit Truecolor"
                ),
                selectedKey = snap.colorDepthBits.toString()
            ) { key ->
                prefs.update { putInt(GeissPreferences.KEY_COLOR_DEPTH, key.toIntOrNull() ?: 8) }
            }

            addSlider(
                card,
                title = "Vertical Screen Size",
                min = 40,
                max = 100,
                initial = snap.verticalSizePct,
                formatter = { "$it%" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_VERTICAL_SIZE, v) }
            }

            addSwitch(
                card,
                title = "Smooth Bilinear Upscaling",
                subtitle = "Turn off for crisp retro pixelated edges",
                initial = snap.linearFilter
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_LINEAR_FILTER, checked) }
            }

            addSlider(
                card,
                title = "Home Screen Swipe Parallax",
                min = 0,
                max = 15,
                initial = (snap.parallaxAmount * 100f).roundToInt(),
                formatter = { if (it == 0) "Off" else "$it%" }
            ) { v ->
                prefs.update { putFloat(GeissPreferences.KEY_PARALLAX, v / 100f) }
            }

            addSwitch(
                card,
                title = "Interactive Touch Energy Bursts",
                subtitle = "Touch or drag on wallpaper to inject glowing plasma",
                initial = snap.touchBurstEnabled
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_TOUCH_BURST, checked) }
            }

            addRadioChoices(
                card,
                title = "Double-Tap Wallpaper Action",
                options = DoubleTapAction.entries.map { it.key to it.label },
                selectedKey = snap.doubleTapAction.key
            ) { key ->
                prefs.update { putString(GeissPreferences.KEY_DOUBLE_TAP, key) }
            }

            addSwitch(
                card,
                title = "Show FPS Counter",
                subtitle = "Display live frame rate in HUD status bar",
                initial = snap.showFps
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_SHOW_FPS, checked) }
            }
        }

        // 3. Visual Dynamics & Palettes (Original Geiss Config)
        addSection(container, "Visual Dynamics & Palettes") { card ->
            addSlider(
                card,
                title = "Auto Mode Switch Interval",
                min = 90,
                max = 2000,
                initial = snap.framesTilAutoSwitch,
                formatter = { "$it frames (~${it / 30}s)" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_AUTO_SWITCH_FRAMES, v) }
            }

            addSlider(
                card,
                title = "Gamma Brightness",
                min = 0,
                max = 100,
                initial = snap.gamma,
                formatter = { "$it" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_GAMMA, v) }
            }

            addSwitch(
                card,
                title = "Sync Palette Shifts to Sound",
                subtitle = "Let audio dynamics govern color palette blending",
                initial = snap.syncColorToSound
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_SYNC_COLOR_SOUND, checked) }
            }

            addSlider(
                card,
                title = "Solar (Monotone) Palette Frequency",
                min = 0,
                max = 5,
                initial = snap.solarPalFreq,
                formatter = { "$it / 5" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_SOLAR_PAL_FREQ, v) }
            }

            addSlider(
                card,
                title = "Coarse Gradient Frequency",
                min = 0,
                max = 10,
                initial = snap.coarsePalFreq,
                formatter = { "$it / 10" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_COARSE_PAL_FREQ, v) }
            }

            addSlider(
                card,
                title = "Beat Slide-Shift Probability",
                min = 0,
                max = 100,
                initial = snap.slideShiftFreq,
                formatter = { "$it%" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_SLIDE_SHIFT_FREQ, v) }
            }

            addSlider(
                card,
                title = "Min Slide-Shift Duration",
                min = 1,
                max = 30,
                initial = snap.slideShiftMinFrames,
                formatter = { "$it frames" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_SLIDE_SHIFT_MIN, v) }
            }
        }

        // 4. Song Title Popups & Messages
        addSection(container, "Song Titles & Overlay Messages") { card ->
            val notifEnabled = GeissMediaListenerService.isNotificationAccessEnabled(this)
            addButton(
                card,
                title = "Track Title Notification Access",
                subtitle = if (notifEnabled) {
                    "Enabled — reads track titles from any music app"
                } else {
                    "Tap to grant Notification Access for automatic song titles"
                }
            ) {
                try {
                    startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
                } catch (_: Exception) {
                    Toast.makeText(
                        this,
                        "Open Android Settings -> Notification Access -> Geiss",
                        Toast.LENGTH_LONG
                    ).show()
                }
            }

            addSwitch(
                card,
                title = "Disable Song Title Popups",
                subtitle = "Turn off automatic track title stamping on track change",
                initial = snap.disableSongTitlePopups
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_DISABLE_SONG_POPUPS, checked) }
            }

            addSwitch(
                card,
                title = "Suppress All Overlay Messages",
                subtitle = "Hide all floating status and title text",
                initial = snap.suppressAllMsg
            ) { checked ->
                prefs.update { putBoolean(GeissPreferences.KEY_SUPPRESS_ALL_MSG, checked) }
            }

            addSlider(
                card,
                title = "Random Song/Message Frequency",
                min = 0,
                max = 10,
                initial = snap.randomSongTitleFreq,
                formatter = { if (it == 0) "Off" else "$it / 10" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_RANDOM_SONG_FREQ, v) }
            }

            addSlider(
                card,
                title = "Message Hold Duration",
                min = 10,
                max = 180,
                initial = snap.songTooltipFrames,
                formatter = { "$it frames (~${String.format("%.1f", it / 30f)}s)" }
            ) { v ->
                prefs.update { putInt(GeissPreferences.KEY_TOOLTIP_FRAMES, v) }
            }
        }

        // 5. Managers & About
        addSection(container, "Managers & Credits") { card ->
            addButton(card, "25 Modes & Star Ratings", "Enable/disable or weight all 25 motion maps") {
                ModesAndPresetsDialogs.showModesDialog(this, controller)
            }
            addButton(card, "Presets (00–99)", "Load, save, or delete saved visual states") {
                ModesAndPresetsDialogs.showPresetsDialog(this, controller)
            }
            addButton(card, "Custom Messages (00–99)", "Manage text messages that dissolve into plasma") {
                ModesAndPresetsDialogs.showCustomMessagesDialog(this, controller)
            }
            addInfoText(
                card,
                "Geiss Live Wallpaper (Android Port of Geiss 4.30)\n" +
                    "Original Geiss Plug-in & Screensaver © 1998–2022 Ryan Geiss (3-Clause BSD License)."
            )
        }
    }

    private fun addSection(
        parent: LinearLayout,
        title: String,
        populate: (LinearLayout) -> Unit
    ) {
        val header = TextView(this).apply {
            text = title
            textSize = 14f
            setTextColor(getColor(R.color.geiss_cyan))
            setPadding(4, 18, 4, 8)
        }
        parent.addView(header)

        val card = MaterialCardView(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            setCardBackgroundColor(getColor(R.color.geiss_surface_solid))
            strokeColor = getColor(R.color.geiss_border)
            strokeWidth = (resources.displayMetrics.density).toInt()
            radius = 16 * resources.displayMetrics.density
        }
        val inner = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 18, 24, 18)
        }
        populate(inner)
        card.addView(inner)
        parent.addView(card)
    }

    private fun addSwitch(
        parent: LinearLayout,
        title: String,
        subtitle: String,
        initial: Boolean,
        onChanged: (Boolean) -> Unit
    ) {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 12, 0, 12)
        }
        val textCol = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        }
        textCol.addView(TextView(this).apply {
            text = title
            textSize = 15f
            setTextColor(getColor(R.color.geiss_text_primary))
        })
        textCol.addView(TextView(this).apply {
            text = subtitle
            textSize = 12f
            setTextColor(getColor(R.color.geiss_text_secondary))
        })
        val sw = MaterialSwitch(this).apply {
            isChecked = initial
            setOnCheckedChangeListener { _, isChecked -> onChanged(isChecked) }
        }
        row.addView(textCol)
        row.addView(sw)
        parent.addView(row)
    }

    private fun addSlider(
        parent: LinearLayout,
        title: String,
        min: Int,
        max: Int,
        initial: Int,
        formatter: (Int) -> String,
        onChanged: (Int) -> Unit
    ) {
        val block = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, 12, 0, 12)
        }
        val clampedInit = initial.coerceIn(min, max)
        val label = TextView(this).apply {
            text = "$title: ${formatter(clampedInit)}"
            textSize = 14f
            setTextColor(getColor(R.color.geiss_text_primary))
        }
        val seek = SeekBar(this).apply {
            this.max = max - min
            progress = clampedInit - min
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                    val value = min + p
                    label.text = "$title: ${formatter(value)}"
                    if (fromUser) onChanged(value)
                }

                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }
        block.addView(label)
        block.addView(seek)
        parent.addView(block)
    }

    private fun addRadioChoices(
        parent: LinearLayout,
        title: String,
        options: List<Pair<String, String>>,
        selectedKey: String,
        onSelected: (String) -> Unit
    ) {
        val block = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, 12, 0, 12)
        }
        block.addView(TextView(this).apply {
            text = title
            textSize = 14f
            setTextColor(getColor(R.color.geiss_text_primary))
            setPadding(0, 0, 0, 6)
        })
        val rg = RadioGroup(this)
        for ((key, label) in options) {
            val rb = RadioButton(this).apply {
                text = label
                setTextColor(getColor(R.color.geiss_text_secondary))
                isChecked = (key == selectedKey)
                setOnClickListener { onSelected(key) }
            }
            rg.addView(rb)
        }
        block.addView(rg)
        parent.addView(block)
    }

    private fun addButton(
        parent: LinearLayout,
        title: String,
        subtitle: String,
        onClick: () -> Unit
    ) {
        val btn = Button(this).apply {
            text = "$title\n$subtitle"
            isAllCaps = false
            setOnClickListener { onClick() }
        }
        parent.addView(btn)
    }

    private fun addInfoText(parent: LinearLayout, body: String) {
        parent.addView(TextView(this).apply {
            text = body
            textSize = 12f
            setTextColor(getColor(R.color.geiss_text_secondary))
            setPadding(4, 14, 4, 4)
        })
    }
}
