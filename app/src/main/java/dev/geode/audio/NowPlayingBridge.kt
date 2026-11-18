package dev.geode.audio

import android.content.ComponentName
import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.provider.Settings
import android.service.notification.NotificationListenerService

class GeodeNotificationListener : NotificationListenerService()

class NowPlayingBridge(
    private val context: Context,
) {
    data class External(
        val packageName: String,
        val appLabel: String,
        val title: String,
        val artist: String,
        val playing: Boolean,
    )

    private val component = ComponentName(context, GeodeNotificationListener::class.java)

    fun hasAccess(): Boolean {
        val enabled =
            runCatching {
                Settings.Secure.getString(context.contentResolver, ENABLED_LISTENERS)
            }.getOrNull().orEmpty()
        return enabled.split(':').any {
            runCatching { ComponentName.unflattenFromString(it) }.getOrNull() == component
        }
    }

    fun settingsIntent(): android.content.Intent =
        android.content
            .Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
            .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)

    /** The other app's session the bridge speaks of and to: the one playing, else the first; none without access. */
    private fun session(): MediaController? {
        if (!hasAccess()) return null
        val manager = context.getSystemService(MediaSessionManager::class.java) ?: return null
        val sessions =
            runCatching { manager.getActiveSessions(component) }.getOrNull().orEmpty()
        return sessions
            .filter { it.packageName != context.packageName }
            .maxByOrNull { if (it.playbackState?.state == PlaybackState.STATE_PLAYING) 1 else 0 }
    }

    /** Play or pause in the other app, whichever it is not doing. */
    fun playPause() {
        val controls = session() ?: return
        if (controls.playbackState?.state == PlaybackState.STATE_PLAYING) {
            controls.transportControls.pause()
        } else {
            controls.transportControls.play()
        }
    }

    fun next() {
        session()?.transportControls?.skipToNext()
    }

    fun previous() {
        session()?.transportControls?.skipToPrevious()
    }

    fun current(): External? {
        val best = session() ?: return null
        val metadata = best.metadata
        return External(
            packageName = best.packageName,
            appLabel = appLabel(best.packageName),
            title =
                metadata
                    ?.getString(android.media.MediaMetadata.METADATA_KEY_TITLE)
                    .orEmpty(),
            artist =
                metadata
                    ?.getString(android.media.MediaMetadata.METADATA_KEY_ARTIST)
                    .orEmpty(),
            playing = best.playbackState?.state == PlaybackState.STATE_PLAYING,
        )
    }

    private fun appLabel(packageName: String): String =
        runCatching {
            val pm = context.packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(packageName, 0)).toString()
        }.getOrNull() ?: packageName

    private companion object {
        const val ENABLED_LISTENERS = "enabled_notification_listeners"
    }
}
