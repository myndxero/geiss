package com.geisswerks.geiss

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.provider.Settings
import android.service.notification.NotificationListenerService

class GeissMediaListenerService : NotificationListenerService() {

    private var sessionManager: MediaSessionManager? = null
    private val activeCallbacks = mutableMapOf<MediaController, MediaController.Callback>()

    private val sessionsListener = MediaSessionManager.OnActiveSessionsChangedListener { controllers ->
        syncControllers(controllers.orEmpty())
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        try {
            val mgr = getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
            sessionManager = mgr
            val cn = ComponentName(this, GeissMediaListenerService::class.java)
            mgr.addOnActiveSessionsChangedListener(sessionsListener, cn)
            syncControllers(mgr.getActiveSessions(cn))
        } catch (_: SecurityException) {
        }
    }

    override fun onListenerDisconnected() {
        clearControllers()
        try {
            sessionManager?.removeOnActiveSessionsChangedListener(sessionsListener)
        } catch (_: Exception) {
        }
        sessionManager = null
        super.onListenerDisconnected()
    }

    private fun syncControllers(controllers: List<MediaController>) {
        clearControllers()
        for (controller in controllers) {
            val cb = object : MediaController.Callback() {
                override fun onMetadataChanged(metadata: MediaMetadata?) {
                    publishFromBestController(controllers)
                }

                override fun onPlaybackStateChanged(state: PlaybackState?) {
                    publishFromBestController(controllers)
                }
            }
            controller.registerCallback(cb)
            activeCallbacks[controller] = cb
        }
        publishFromBestController(controllers)
    }

    private fun clearControllers() {
        for ((controller, cb) in activeCallbacks) {
            try {
                controller.unregisterCallback(cb)
            } catch (_: Exception) {
            }
        }
        activeCallbacks.clear()
    }

    private fun publishFromBestController(controllers: List<MediaController>) {
        val active = controllers.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: controllers.firstOrNull()

        val meta = active?.metadata ?: return
        val title = meta.getString(MediaMetadata.METADATA_KEY_TITLE)?.trim().orEmpty()
        val artist = meta.getString(MediaMetadata.METADATA_KEY_ARTIST)?.trim().orEmpty()
        if (title.isEmpty()) return

        val formatted = if (artist.isNotEmpty() && !title.contains(artist, ignoreCase = true)) {
            "$artist - $title"
        } else {
            title
        }
        GeissController.getInstance(applicationContext).onDetectedSongChanged(formatted)
    }

    companion object {
        fun isNotificationAccessEnabled(context: Context): Boolean {
            val flat = Settings.Secure.getString(
                context.contentResolver,
                "enabled_notification_listeners"
            ) ?: return false
            val component = ComponentName(context, GeissMediaListenerService::class.java).flattenToString()
            return flat.split(':').any { it.equals(component, ignoreCase = true) }
        }
    }
}
