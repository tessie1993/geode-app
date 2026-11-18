package dev.geode.audio

import android.app.NotificationManager
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.view.KeyEvent

class GeodeNotificationListener : NotificationListenerService()

/**
 * Another app's media session: what it plays and its transport. Reading other apps' sessions is gated on this app
 * being an enabled notification listener ([GeodeNotificationListener]), which the person grants in system settings.
 *
 * While [watch]ed, the platform calls back on the main thread when the sessions change and when the chosen one's
 * metadata or playback state does, and [onChange] hears the result there. Main thread, except [hasAccess], the
 * transport calls and [positionMs], which are safe on any thread.
 */
class NowPlayingBridge(
    private val context: Context,
    private val onChange: (External?) -> Unit = {},
) {
    /**
     * What the player shows of the other app. Equal while only its playhead moves, so a session that reports its
     * position every second does not recompose the player: the playhead is read through [positionMs] instead.
     */
    data class External(
        val packageName: String,
        val appLabel: String,
        val title: String,
        val artist: String,
        val art: Bitmap? = null,
        val durationMs: Long = 0L,
        val playing: Boolean = false,
        val controls: ExternalControls = ExternalControls(),
    )

    private val component = ComponentName(context, GeodeNotificationListener::class.java)
    private val main = Handler(Looper.getMainLooper())
    private val manager: MediaSessionManager? get() = context.getSystemService(MediaSessionManager::class.java)

    @Volatile
    private var watched: MediaController? = null

    private var watching = false

    /** The chosen session's metadata, read once per change, so its artwork stays one object while only the state moves. */
    @Volatile
    private var shown: External? = null

    /** The chosen session's playback state as its callback last gave it: read without a binder call per poll or tap. */
    @Volatile
    private var playback: PlaybackState? = null

    fun hasAccess(): Boolean =
        runCatching {
            context.getSystemService(NotificationManager::class.java)?.isNotificationListenerAccessGranted(component) == true
        }.getOrDefault(false)

    /** This listener's own page in system settings, else the list of notification listeners. */
    fun openAccessSettings(from: Context) {
        val detail =
            Intent(Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS)
                .putExtra(Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME, component.flattenToString())
        val list = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
        for (intent in listOf(detail, list)) {
            if (from !is android.app.Activity) intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            val opened =
                try {
                    from.startActivity(intent)
                    true
                } catch (_: ActivityNotFoundException) {
                    // A build without the detail page: the list is the way there.
                    false
                } catch (_: SecurityException) {
                    false
                }
            if (opened) return
        }
    }

    private val sessionsChanged =
        MediaSessionManager.OnActiveSessionsChangedListener { sessions -> choose(sessions.orEmpty()) }

    private val callback =
        object : MediaController.Callback() {
            override fun onMetadataChanged(metadata: MediaMetadata?) {
                shown = null
                publish()
            }

            override fun onPlaybackStateChanged(state: PlaybackState?) {
                playback = state
                publish()
            }

            override fun onSessionDestroyed() = rechoose()
        }

    /** Follows the other app's session until [unwatch]. Without notification access it does nothing and returns false. */
    fun watch(): Boolean {
        if (watching) return true
        val sessions = manager ?: return false
        val added =
            runCatching { sessions.addOnActiveSessionsChangedListener(sessionsChanged, component, main) }.isSuccess
        if (!added) return false
        watching = true
        rechoose()
        return true
    }

    fun unwatch() {
        if (watching) runCatching { manager?.removeOnActiveSessionsChangedListener(sessionsChanged) }
        watching = false
        follow(null)
    }

    private fun rechoose() {
        if (!watching) return
        choose(runCatching { manager?.getActiveSessions(component) }.getOrNull().orEmpty())
    }

    private fun choose(sessions: List<MediaController>) {
        if (!watching) return
        val pick =
            ExternalSessionRules.choose(
                sessions.map { ExternalSessionRules.Candidate(it.packageName, it.playbackState?.state) },
                context.packageName,
                watched?.packageName,
            )
        follow(sessions.getOrNull(pick))
    }

    private fun follow(controller: MediaController?) {
        val old = watched
        if (controller != null && old != null && controller.sessionToken == old.sessionToken) {
            publish()
            return
        }
        old?.let { runCatching { it.unregisterCallback(callback) } }
        watched = controller
        shown = null
        playback = controller?.playbackState
        controller?.let { runCatching { it.registerCallback(callback, main) } }
        publish()
    }

    private fun publish() {
        val controller = watched
        if (controller == null) {
            onChange(null)
            return
        }
        val base = shown ?: describe(controller).also { shown = it }
        val now = playback
        val state = now?.state
        onChange(
            base.copy(
                playing = ExternalSessionRules.isPlaying(state),
                controls = ExternalSessionRules.controls(now?.actions ?: 0L, state, base.durationMs),
            ),
        )
    }

    private fun describe(controller: MediaController): External {
        val metadata = controller.metadata
        return External(
            packageName = controller.packageName,
            appLabel = appLabel(controller.packageName),
            title =
                metadata?.text(MediaMetadata.METADATA_KEY_TITLE)
                    ?: metadata?.text(MediaMetadata.METADATA_KEY_DISPLAY_TITLE).orEmpty(),
            artist =
                metadata?.text(MediaMetadata.METADATA_KEY_ARTIST)
                    ?: metadata?.text(MediaMetadata.METADATA_KEY_ALBUM_ARTIST).orEmpty(),
            art = metadata?.art(),
            durationMs = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION)?.coerceAtLeast(0L) ?: 0L,
        )
    }

    private fun MediaMetadata.text(key: String): String? = getText(key)?.toString()?.takeIf { it.isNotBlank() }

    private fun MediaMetadata.art(): Bitmap? =
        getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)
            ?: getBitmap(MediaMetadata.METADATA_KEY_ART)
            ?: getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON)

    /**
     * One look, without [watch]ing: the other app's session as [ExternalSessionRules.choose] picks it, none
     * without access. For a reader that asks now and then (the screen saver's line), not for the player.
     */
    fun current(): External? {
        if (!hasAccess()) return null
        val sessions = runCatching { manager?.getActiveSessions(component) }.getOrNull().orEmpty()
        val pick =
            ExternalSessionRules.choose(
                sessions.map { ExternalSessionRules.Candidate(it.packageName, it.playbackState?.state) },
                context.packageName,
                null,
            )
        val controller = sessions.getOrNull(pick) ?: return null
        val reported = controller.playbackState
        return describe(controller).copy(
            playing = ExternalSessionRules.isPlaying(reported?.state),
            controls = ExternalSessionRules.controls(reported?.actions ?: 0L, reported?.state, 0L),
        )
    }

    /** The other app's playhead now, or null while none is watched. */
    fun positionMs(): Long? {
        if (watched == null) return null
        val now = playback ?: return 0L
        return ExternalSessionRules.positionAt(
            now.position,
            now.lastPositionUpdateTime,
            now.playbackSpeed,
            ExternalSessionRules.isPlaying(now.state),
            shown?.durationMs ?: 0L,
            SystemClock.elapsedRealtime(),
        )
    }

    /** Play or pause in the other app, whichever it is not doing, as its session allows. */
    fun playPause() {
        val controller = watched ?: return
        val now = playback
        when (ExternalSessionRules.toggle(now?.actions ?: 0L, now?.state)) {
            ExternalToggle.PLAY -> controller.transportControls.play()
            ExternalToggle.PAUSE -> controller.transportControls.pause()
            ExternalToggle.PLAY_PAUSE_KEY -> {
                controller.dispatchMediaButtonEvent(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE))
                controller.dispatchMediaButtonEvent(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE))
            }
            ExternalToggle.NONE -> Unit
        }
    }

    fun next() {
        if (allowed()?.next == true) watched?.transportControls?.skipToNext()
    }

    fun previous() {
        if (allowed()?.previous == true) watched?.transportControls?.skipToPrevious()
    }

    /** Seeks the other app to [fraction] of its track, when it takes seeks. */
    fun seekTo(fraction: Float) {
        val controller = watched ?: return
        val duration = shown?.durationMs ?: return
        if (allowed()?.seek != true) return
        controller.transportControls.seekTo((fraction.coerceIn(0f, 1f) * duration).toLong())
    }

    private fun allowed(): ExternalControls? {
        if (watched == null) return null
        val now = playback
        return ExternalSessionRules.controls(now?.actions ?: 0L, now?.state, shown?.durationMs ?: 0L)
    }

    private fun appLabel(packageName: String): String =
        runCatching {
            val pm = context.packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(packageName, 0)).toString()
        }.getOrNull() ?: packageName
}
