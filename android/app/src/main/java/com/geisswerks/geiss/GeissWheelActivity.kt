package com.geisswerks.geiss

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class GeissWheelActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val controller = GeissController.getInstance(this)
        val wheel = GeissActionWheelView(this)
        val tapX = intent.getFloatExtra(EXTRA_TAP_X, -1f)
        val tapY = intent.getFloatExtra(EXTRA_TAP_Y, -1f)
        if (tapX > 0f && tapY > 0f) {
            wheel.setAnchor(tapX, tapY)
        }
        wheel.updateActiveFlags(controller.primaryStatus(), controller.prefs.getSnapshot())

        wheel.onActionSelected = { action ->
            if (action == GeissActionWheelView.WheelAction.OPEN_SETTINGS) {
                startActivity(Intent(this, SettingsActivity::class.java))
            } else {
                val feedback = controller.executeWheelAction(action)
                if (feedback != null) {
                    Toast.makeText(this, feedback, Toast.LENGTH_SHORT).show()
                }
            }
            finish()
            @Suppress("DEPRECATION")
            overridePendingTransition(0, 0)
        }
        wheel.onDismissRequested = {
            finish()
            @Suppress("DEPRECATION")
            overridePendingTransition(0, 0)
        }

        setContentView(wheel)
        wheel.showAnimated()
    }

    companion object {
        const val EXTRA_TAP_X = "tap_x"
        const val EXTRA_TAP_Y = "tap_y"
    }
}
