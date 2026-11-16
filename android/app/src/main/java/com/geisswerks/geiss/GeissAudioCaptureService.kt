package com.geisswerks.geiss

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

class GeissAudioCaptureService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            GeissAudioManager.setMediaProjection(null)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }

        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        val resultCode = intent?.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED)
            ?: Activity.RESULT_CANCELED
        @Suppress("DEPRECATION")
        val dataIntent: Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent?.getParcelableExtra(EXTRA_RESULT_DATA, Intent::class.java)
        } else {
            intent?.getParcelableExtra(EXTRA_RESULT_DATA)
        }

        if (resultCode == Activity.RESULT_OK && dataIntent != null) {
            val mgr = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
            val projection = mgr.getMediaProjection(resultCode, dataIntent)
            GeissAudioManager.setMediaProjection(projection)
        } else {
            stopSelf()
        }

        return START_NOT_STICKY
    }

    override fun onDestroy() {
        GeissAudioManager.setMediaProjection(null)
        super.onDestroy()
    }

    private fun ensureChannel() {
        val mgr = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Geiss System Audio Capture",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Required by Android 10+ to capture system music audio cleanly"
            setShowBadge(false)
        }
        mgr.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stopIntent = PendingIntent.getService(
            this,
            1,
            Intent(this, GeissAudioCaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Geiss System Audio Active")
            .setContentText("Visualizing stereo music playback in real time")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setOngoing(true)
            .setContentIntent(openIntent)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop Capture", stopIntent)
            .build()
    }

    companion object {
        const val CHANNEL_ID = "geiss_audio_capture"
        const val NOTIFICATION_ID = 4301
        const val ACTION_START = "com.geisswerks.geiss.ACTION_START_PROJECTION"
        const val ACTION_STOP = "com.geisswerks.geiss.ACTION_STOP_PROJECTION"
        const val EXTRA_RESULT_CODE = "result_code"
        const val EXTRA_RESULT_DATA = "result_data"

        fun startWithProjection(context: Context, resultCode: Int, data: Intent) {
            val intent = Intent(context, GeissAudioCaptureService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_RESULT_CODE, resultCode)
                putExtra(EXTRA_RESULT_DATA, data)
            }
            context.startForegroundService(intent)
        }

        fun stop(context: Context) {
            context.startService(Intent(context, GeissAudioCaptureService::class.java).setAction(ACTION_STOP))
        }
    }
}
