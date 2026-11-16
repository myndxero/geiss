package com.geisswerks.geiss

import android.app.AlertDialog
import android.content.Context
import android.os.Build
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.RatingBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

object ModesAndPresetsDialogs {

    fun showModesDialog(context: Context, controller: GeissController) {
        val prefs = controller.prefs
        val ratings = prefs.getModeRatings()
        val currentMode = controller.primaryStatus()?.currentMode ?: -1

        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 16, 24, 16)
        }

        val hint = TextView(context).apply {
            text = "Tap a mode to jump immediately. Star ratings (0★–5★) control how often each mode plays automatically (0★ disables a mode)."
            textSize = 12f
            setPadding(0, 0, 0, 16)
        }
        container.addView(hint)

        val ratingBars = arrayOfNulls<RatingBar>(GeissNative.NUM_MODES + 1)
        var dialog: AlertDialog? = null

        for (modeId in 1..GeissNative.NUM_MODES) {
            val row = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(12, 10, 12, 10)
            }
            val titleRow = LinearLayout(context).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val modeName = GeissNative.nativeGetModeName(modeId)
            val prefix = if (modeId == currentMode) "▶ " else ""
            val titleTv = TextView(context).apply {
                layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                text = "$prefix${modeId.toString().padStart(2, '0')}. $modeName"
                textSize = 14f
                if (modeId == currentMode) {
                    setTextColor(context.getColor(R.color.geiss_cyan))
                }
            }
            val playBtn = Button(context).apply {
                text = "Play"
                textSize = 11f
                setOnClickListener {
                    controller.selectMap(modeId)
                    dialog?.dismiss()
                }
            }
            titleRow.addView(titleTv)
            titleRow.addView(playBtn)

            val bar = RatingBar(context, null, android.R.attr.ratingBarStyleIndicator).apply {
                layoutParams = LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
                numStars = 5
                stepSize = 1f
                setIsIndicator(false)
                rating = ratings[modeId].toFloat()
                setOnRatingBarChangeListener { _, r, fromUser ->
                    if (fromUser) {
                        prefs.setModeRating(modeId, r.toInt())
                    }
                }
            }
            ratingBars[modeId] = bar
            row.addView(titleRow)
            row.addView(bar)
            container.addView(row)
        }

        val scroll = ScrollView(context).apply { addView(container) }
        dialog = AlertDialog.Builder(context)
            .setTitle("25 Geiss Modes & Ratings")
            .setView(scroll)
            .setNeutralButton("Reset All 3★") { _, _ ->
                for (i in 1..GeissNative.NUM_MODES) {
                    prefs.setModeRating(i, 3)
                }
                Toast.makeText(context, "All 25 modes reset to 3★", Toast.LENGTH_SHORT).show()
            }
            .setPositiveButton("Done", null)
            .show()
    }

    fun showPresetsDialog(context: Context, controller: GeissController) {
        val prefs = controller.prefs
        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 16, 24, 16)
        }
        val info = TextView(context).apply {
            text = "100 Classic Preset Slots (00–99). Save snapshots any active map, palette, waveform, spectrum, and effects state."
            textSize = 12f
            setPadding(0, 0, 0, 12)
        }
        container.addView(info)

        val slotsList = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        container.addView(slotsList)

        var dialog: AlertDialog? = null

        fun rebuildList() {
            slotsList.removeAllViews()
            for (slot in 0 until GeissNative.NUM_PRESET_SLOTS) {
                val preset = prefs.getPreset(slot)
                val row = LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                    setPadding(4, 6, 4, 6)
                }
                val slotStr = slot.toString().padStart(2, '0')
                val label = TextView(context).apply {
                    layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    text = if (preset != null) {
                        "$slotStr · ${preset.name} (Mode ${preset.mode})"
                    } else {
                        "$slotStr · (Empty)"
                    }
                    textSize = 13f
                    alpha = if (preset != null) 1.0f else 0.55f
                }
                val loadBtn = Button(context).apply {
                    text = "Load"
                    textSize = 11f
                    isEnabled = preset != null
                    setOnClickListener {
                        controller.loadPreset(slot)
                        dialog?.dismiss()
                    }
                }
                val saveBtn = Button(context).apply {
                    text = "Save"
                    textSize = 11f
                    setOnClickListener {
                        val nameInput = EditText(context).apply {
                            hint = "Preset name"
                            setText(preset?.name ?: "Custom $slotStr")
                        }
                        AlertDialog.Builder(context)
                            .setTitle("Save Slot $slotStr")
                            .setView(nameInput)
                            .setPositiveButton("Save") { _, _ ->
                                val saved = controller.savePreset(slot, nameInput.text.toString())
                                if (saved != null) {
                                    Toast.makeText(context, "Saved slot $slotStr", Toast.LENGTH_SHORT).show()
                                    rebuildList()
                                }
                            }
                            .setNegativeButton("Cancel", null)
                            .show()
                    }
                }
                row.addView(label)
                row.addView(loadBtn)
                row.addView(saveBtn)
                if (preset != null) {
                    val delBtn = Button(context).apply {
                        text = "✕"
                        textSize = 11f
                        setOnClickListener {
                            prefs.deletePreset(slot)
                            rebuildList()
                        }
                    }
                    row.addView(delBtn)
                }
                slotsList.addView(row)
            }
        }

        rebuildList()
        val scroll = ScrollView(context).apply { addView(container) }
        dialog = AlertDialog.Builder(context)
            .setTitle("Geiss Presets (00–99)")
            .setView(scroll)
            .setPositiveButton("Close", null)
            .show()
    }

    fun showCustomMessagesDialog(context: Context, controller: GeissController) {
        val prefs = controller.prefs
        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 16, 24, 16)
        }
        val info = TextView(context).apply {
            text = "100 Custom Message Slots (00–99). Stamp any message directly into the dissolving Geiss plasma buffer."
            textSize = 12f
            setPadding(0, 0, 0, 12)
        }
        container.addView(info)

        val listLayout = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        container.addView(listLayout)

        var dialog: AlertDialog? = null

        fun rebuild() {
            listLayout.removeAllViews()
            for (slot in 0 until GeissNative.NUM_PRESET_SLOTS) {
                val msg = prefs.getCustomMessage(slot)
                val slotStr = slot.toString().padStart(2, '0')
                val row = LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                    setPadding(4, 6, 4, 6)
                }
                val label = TextView(context).apply {
                    layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                    text = if (msg.isNotEmpty()) "$slotStr · $msg" else "$slotStr · (Empty)"
                    textSize = 13f
                    alpha = if (msg.isNotEmpty()) 1f else 0.55f
                }
                val showBtn = Button(context).apply {
                    text = "Show"
                    textSize = 11f
                    isEnabled = msg.isNotEmpty()
                    setOnClickListener {
                        controller.showCustomMessage(slot)
                        dialog?.dismiss()
                    }
                }
                val editBtn = Button(context).apply {
                    text = "Edit"
                    textSize = 11f
                    setOnClickListener {
                        val input = EditText(context).apply {
                            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
                            setText(msg)
                            hint = "Enter message (blank to clear)"
                        }
                        AlertDialog.Builder(context)
                            .setTitle("Message Slot $slotStr")
                            .setView(input)
                            .setPositiveButton("Save") { _, _ ->
                                prefs.setCustomMessage(slot, input.text.toString())
                                rebuild()
                            }
                            .setNegativeButton("Cancel", null)
                            .show()
                    }
                }
                row.addView(label)
                row.addView(showBtn)
                row.addView(editBtn)
                listLayout.addView(row)
            }
        }

        rebuild()
        val scroll = ScrollView(context).apply { addView(container) }
        dialog = AlertDialog.Builder(context)
            .setTitle("Custom Messages (00–99)")
            .setView(scroll)
            .setPositiveButton("Close", null)
            .show()
    }

    fun showAudioSourceDialog(
        context: Context,
        controller: GeissController,
        onRequestAudioPermission: () -> Unit,
        onRequestPlaybackCapture: () -> Unit
    ) {
        val current = controller.prefs.getSnapshot().audioSourceMode
        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(36, 20, 36, 8)
        }

        val statusTv = TextView(context).apply {
            text = "Current state: ${GeissAudioManager.status.message}"
            textSize = 13f
            setPadding(0, 0, 0, 16)
        }
        container.addView(statusTv)

        val radioGroup = RadioGroup(context)
        val modeById = mutableMapOf<Int, AudioSourceMode>()
        var checkedId = View.NO_ID
        for (mode in AudioSourceMode.entries) {
            if (mode == AudioSourceMode.PLAYBACK_CAPTURE && Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                continue
            }
            val rbId = View.generateViewId()
            val rb = RadioButton(context).apply {
                id = rbId
                text = "${mode.label}\n${mode.description}"
                setPadding(8, 12, 8, 12)
            }
            radioGroup.addView(rb)
            modeById[rbId] = mode
            if (mode == current) {
                checkedId = rbId
            }
        }
        if (checkedId != View.NO_ID) {
            radioGroup.check(checkedId)
        }
        container.addView(radioGroup)

        AlertDialog.Builder(context)
            .setTitle("Music Audio Capture Source")
            .setView(container)
            .setPositiveButton("Apply") { _, _ ->
                val selected = modeById[radioGroup.checkedRadioButtonId]
                    ?: AudioSourceMode.OUTPUT_MIX
                controller.prefs.update { putString(GeissPreferences.KEY_AUDIO_SOURCE, selected.key) }
                if (!GeissAudioManager.hasRecordAudioPermission(context)) {
                    onRequestAudioPermission()
                } else if (selected == AudioSourceMode.PLAYBACK_CAPTURE && !GeissAudioManager.hasMediaProjection()) {
                    onRequestPlaybackCapture()
                } else {
                    GeissAudioManager.refreshCapture()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
}
