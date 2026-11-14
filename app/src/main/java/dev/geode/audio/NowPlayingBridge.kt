package dev.geode.audio

import android.content.ComponentName
import android.content.Context
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.provider.Settings
import android.service.notification.NotificationListenerService
import dev.geode.engine.bridge.GeodeNative

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

    fun current(): External? {
        if (!hasAccess()) return null
        val manager = context.getSystemService(MediaSessionManager::class.java) ?: return null
        val sessions =
            runCatching { manager.getActiveSessions(component) }.getOrNull().orEmpty()
        val others = sessions.filter { it.packageName != context.packageName }
        // Which one to name is native's (core/sound/NowPlaying); this lists them and says which play.
        val playing = IntArray(others.size) { if (others[it].playbackState?.state == PlaybackState.STATE_PLAYING) 1 else 0 }
        val best = others.getOrNull(GeodeNative.nowPlayingPick(playing)) ?: return null
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
