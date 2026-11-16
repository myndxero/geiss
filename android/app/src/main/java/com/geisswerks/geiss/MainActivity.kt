package com.geisswerks.geiss

import android.Manifest
import android.annotation.SuppressLint
import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.geisswerks.geiss.databinding.ActivityMainBinding
import com.google.android.material.button.MaterialButton

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var controller: GeissController
    private lateinit var renderer: GeissGlRenderer

    private val effectButtons = mutableListOf<MaterialButton>()
    private var hudVisible = true
    private var currentSong = ""

    private val requestRecordAudioLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) {
                GeissAudioManager.refreshCapture()
                val snap = controller.prefs.getSnapshot()
                if (snap.audioSourceMode == AudioSourceMode.PLAYBACK_CAPTURE &&
                    !GeissAudioManager.hasMediaProjection()
                ) {
                    launchProjectionPrompt()
                }
            } else {
                Toast.makeText(
                    this,
                    "Audio permission denied — running auto-demo waveform",
                    Toast.LENGTH_LONG
                ).show()
            }
        }

    private val requestNotificationPermLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    private val requestProjectionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == RESULT_OK && result.data != null) {
                GeissAudioCaptureService.startWithProjection(this, result.resultCode, result.data!!)
            } else {
                Toast.makeText(
                    this,
                    "System Audio Cast prompt canceled — using Global Output Mix",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

    private val statusListener: (GeissNative.EngineStatus) -> Unit = { st ->
        runOnUiThread { updateUiStatus(st) }
    }

    private val songListener: (String) -> Unit = { title ->
        currentSong = title
        runOnUiThread { updateAudioBadge() }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        controller = GeissController.getInstance(this)
        renderer = GeissGlRenderer(this)

        setupGlSurface()
        setupControls()
        ensurePermissions()
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupGlSurface() {
        binding.glSurfaceView.setEGLContextClientVersion(2)
        binding.glSurfaceView.preserveEGLContextOnPause = true
        binding.glSurfaceView.setRenderer(renderer)
        binding.glSurfaceView.renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY

        val gestureDetector = GestureDetector(
            this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onDoubleTap(e: MotionEvent): Boolean {
                    when (controller.prefs.getSnapshot().doubleTapAction) {
                        DoubleTapAction.NEXT_MAP -> controller.nextMap()
                        DoubleTapAction.RANDOM_PALETTE -> controller.randomizePalette()
                        DoubleTapAction.SONG_POPUP -> controller.triggerSongTitlePopup()
                        DoubleTapAction.TOGGLE_LOCK -> controller.toggleLockMode()
                        DoubleTapAction.NONE -> Unit
                    }
                    return true
                }
            }
        )

        binding.glSurfaceView.setOnTouchListener { v, event ->
            gestureDetector.onTouchEvent(event)
            if (event.actionMasked == MotionEvent.ACTION_DOWN ||
                event.actionMasked == MotionEvent.ACTION_MOVE
            ) {
                val w = v.width.coerceAtLeast(1).toFloat()
                val h = v.height.coerceAtLeast(1).toFloat()
                val strength = if (event.actionMasked == MotionEvent.ACTION_DOWN) 1.0f else 0.55f
                renderer.triggerTouchBurst(
                    (event.x / w).coerceIn(0f, 1f),
                    (event.y / h).coerceIn(0f, 1f),
                    strength
                )
            }
            true
        }
    }

    private fun setupControls() {
        binding.btnSetWallpaper.setOnClickListener { openLiveWallpaperPicker() }
        binding.btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        binding.btnHideHud.setOnClickListener { setHudVisible(false) }
        binding.btnShowHud.setOnClickListener { setHudVisible(true) }

        binding.tvAudioBadge.setOnClickListener { openAudioSourceDialog() }
        binding.btnAudioSourceDialog.setOnClickListener { openAudioSourceDialog() }

        binding.btnNextMap.setOnClickListener { controller.nextMap() }
        binding.btnRandomPalette.setOnClickListener { controller.randomizePalette() }
        binding.btnLockMap.setOnClickListener { controller.toggleLockMode() }
        binding.btnLockPalette.setOnClickListener { controller.toggleLockPalette() }
        binding.btnSlideShift.setOnClickListener { controller.toggleSlideShift() }
        binding.btnSongPopup.setOnClickListener { controller.triggerSongTitlePopup() }
        binding.btnRefreshEdges.setOnClickListener { controller.refreshEdges() }
        binding.btnWaveform.setOnClickListener { controller.cycleWaveform() }
        binding.btnSpectrum.setOnClickListener { controller.toggleSpectrumMode() }

        binding.btnModesDialog.setOnClickListener {
            ModesAndPresetsDialogs.showModesDialog(this, controller)
        }
        binding.btnPresetsDialog.setOnClickListener {
            ModesAndPresetsDialogs.showPresetsDialog(this, controller)
        }
        binding.btnMessagesDialog.setOnClickListener {
            ModesAndPresetsDialogs.showCustomMessagesDialog(this, controller)
        }

        binding.seekVolumePos.progress = controller.prefs.getSnapshot().volpos
        binding.tvVolLabel.text = "Gain ${binding.seekVolumePos.progress}"
        binding.seekVolumePos.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                binding.tvVolLabel.text = "Gain $progress"
                if (fromUser) {
                    controller.setVolumePos(progress)
                }
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // Build all 8 classic Geiss Effect toggle buttons dynamically
        val density = resources.displayMetrics.density
        val marginEndPx = (6 * density).toInt()
        val heightPx = (32 * density).toInt()
        val padH = (10 * density).toInt()

        for (i in 0 until GeissNative.NUM_EFFECTS) {
            val btn = MaterialButton(
                this,
                null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle
            ).apply {
                text = GeissNative.EFFECT_NAMES[i]
                textSize = 11f
                minWidth = 0
                minimumWidth = 0
                insetTop = 0
                insetBottom = 0
                setPadding(padH, 0, padH, 0)
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    heightPx
                ).apply {
                    marginEnd = marginEndPx
                }
                setOnClickListener { controller.toggleEffect(i) }
            }
            effectButtons.add(btn)
            binding.effectsRow.addView(btn)
        }
    }

    private fun setHudVisible(visible: Boolean) {
        hudVisible = visible
        binding.topHud.visibility = if (visible) View.VISIBLE else View.GONE
        binding.bottomHud.visibility = if (visible) View.VISIBLE else View.GONE
        binding.btnShowHud.visibility = if (visible) View.GONE else View.VISIBLE
    }

    private fun ensurePermissions() {
        if (!GeissAudioManager.hasRecordAudioPermission(this)) {
            requestRecordAudioLauncher.launch(Manifest.permission.RECORD_AUDIO)
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val notifPerm = ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            if (notifPerm != android.content.pm.PackageManager.PERMISSION_GRANTED &&
                controller.prefs.getSnapshot().audioSourceMode == AudioSourceMode.PLAYBACK_CAPTURE
            ) {
                requestNotificationPermLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    private fun openAudioSourceDialog() {
        ModesAndPresetsDialogs.showAudioSourceDialog(
            context = this,
            controller = controller,
            onRequestAudioPermission = { requestRecordAudioLauncher.launch(Manifest.permission.RECORD_AUDIO) },
            onRequestPlaybackCapture = { launchProjectionPrompt() }
        )
    }

    private fun launchProjectionPrompt() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        requestProjectionLauncher.launch(mgr.createScreenCaptureIntent())
    }

    private fun openLiveWallpaperPicker() {
        val component = ComponentName(this, GeissWallpaperService::class.java)
        val intent = Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).apply {
            putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT, component)
        }
        try {
            startActivity(intent)
        } catch (_: Exception) {
            try {
                startActivity(Intent(WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER))
            } catch (_: Exception) {
                Toast.makeText(
                    this,
                    "Open System Settings -> Wallpaper -> Live Wallpapers -> Geiss",
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    private fun updateUiStatus(st: GeissNative.EngineStatus) {
        val modeId = st.currentMode.toString().padStart(2, '0')
        val modeName = GeissNative.nativeGetModeName(st.currentMode)
        binding.tvModeTitle.text = "Mode $modeId: $modeName"

        val waveStr = if (st.currentWaveform == 0) "Wave Off" else "Wave ${st.currentWaveform}"
        val visStr = if (st.isSpectrumMode) "Spectrum" else waveStr
        val soundStr = if (st.isSoundActive) "Audio Active" else "Silent"
        binding.tvStatusSub.text =
            "${st.colorDepthBits}-bit · ${st.fps.toInt()} FPS · $visStr · $soundStr"

        binding.btnWaveform.text = "Wave ${st.currentWaveform}"
        styleToggle(binding.btnLockMap, st.isModeLocked, "Lock Map")
        styleToggle(binding.btnLockPalette, st.isPaletteLocked, "Lock Pal")
        styleToggle(binding.btnSlideShift, st.isSlideShift, "Shift")
        styleToggle(binding.btnSpectrum, st.isSpectrumMode, "Spectrum")

        if (binding.seekVolumePos.progress != st.volpos) {
            binding.seekVolumePos.progress = st.volpos
            binding.tvVolLabel.text = "Gain ${st.volpos}"
        }

        for (i in effectButtons.indices) {
            styleToggle(effectButtons[i], st.effects[i], GeissNative.EFFECT_NAMES[i])
        }
    }

    private fun styleToggle(btn: MaterialButton, active: Boolean, label: String) {
        btn.text = label
        if (active) {
            btn.setTextColor(getColor(R.color.geiss_cyan))
            btn.strokeColor = android.content.res.ColorStateList.valueOf(getColor(R.color.geiss_cyan))
        } else {
            btn.setTextColor(getColor(R.color.geiss_text_secondary))
            btn.strokeColor = android.content.res.ColorStateList.valueOf(getColor(R.color.geiss_border))
        }
    }

    private fun updateAudioBadge() {
        val audioMsg = GeissAudioManager.status.message
        val songPart = if (currentSong.isNotBlank()) " · ♫ $currentSong" else ""
        binding.tvAudioBadge.text = "$audioMsg$songPart"
        val colorRes = when (GeissAudioManager.status) {
            GeissAudioManager.CaptureStatus.NEEDS_RECORD_AUDIO,
            GeissAudioManager.CaptureStatus.ERROR -> R.color.geiss_magenta
            GeissAudioManager.CaptureStatus.NEEDS_PROJECTION -> R.color.geiss_amber
            else -> R.color.geiss_green
        }
        binding.tvAudioBadge.setTextColor(getColor(colorRes))
    }

    override fun onResume() {
        super.onResume()
        renderer.setActive(true)
        binding.glSurfaceView.onResume()
        controller.addUiStatusListener(statusListener)
        controller.addSongListener(songListener)
        GeissAudioManager.setStatusListener {
            runOnUiThread { updateAudioBadge() }
        }
        updateAudioBadge()
    }

    override fun onPause() {
        GeissAudioManager.setStatusListener(null)
        controller.removeSongListener(songListener)
        controller.removeUiStatusListener(statusListener)
        binding.glSurfaceView.onPause()
        renderer.setActive(false)
        super.onPause()
    }

    override fun onDestroy() {
        renderer.destroy()
        super.onDestroy()
    }
}
