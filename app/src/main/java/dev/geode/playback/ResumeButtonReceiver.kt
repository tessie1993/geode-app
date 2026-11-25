package dev.geode.playback

import android.content.Context
import android.content.Intent
import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.MediaButtonReceiver
import dev.geode.data.SessionStore

/**
 * Media3's media-button receiver, declining a play key there is nothing to play for. WHY: the
 * receiver answers the key with startForegroundService, and the service reaches the foreground only
 * once something plays. With an empty player and no saved queue nothing ever does, and the system
 * kills the app (ForegroundServiceDidNotStartInTimeException). [shouldStartForegroundService] is
 * the hook Media3 gives for that.
 */
@OptIn(UnstableApi::class)
class ResumeButtonReceiver : MediaButtonReceiver() {
    override fun shouldStartForegroundService(
        context: Context,
        intent: Intent,
    ): Boolean = playKeyHasSomethingToPlay(PlaybackEngine.hasQueue()) { ResumeSource.of(SessionStore(context).load()) }
}

/** A live queue plays as it is; without one the key plays only what [saved] can resume. */
internal inline fun playKeyHasSomethingToPlay(
    liveQueue: Boolean,
    saved: () -> ResumeSource,
): Boolean = liveQueue || saved() is ResumeSource.Queue
