package dev.geode.playback

import androidx.media3.common.C
import androidx.media3.common.Player
import dev.geode.data.BookmarkStore

/** Keeps a long track's bookmark current from the session's poll, and seeks to it when the track starts. */
class TrackBookmarks(
    private val store: BookmarkStore,
    private val enabled: () -> Boolean,
) {
    private var lastSavedAtMs = 0L
    private var resumedUri: String? = null

    fun onTrackStarted(player: Player) {
        val uri = player.currentMediaItem?.localConfiguration?.uri?.toString() ?: return
        if (uri == resumedUri) return
        resumedUri = uri
        if (!enabled()) return
        val position = store.positionFor(uri) ?: return
        if (player.currentPosition < BookmarkStore.EDGE_MS) player.seekTo(position)
    }

    fun onPoll(player: Player) {
        if (!enabled()) return
        val now = System.currentTimeMillis()
        if (now - lastSavedAtMs < SAVE_EVERY_MS) return
        val uri = player.currentMediaItem?.localConfiguration?.uri?.toString() ?: return
        val duration = player.duration
        if (duration == C.TIME_UNSET || duration < BookmarkStore.LONG_TRACK_MS) return
        lastSavedAtMs = now
        store.note(uri, player.currentPosition, duration)
    }

    private companion object {
        const val SAVE_EVERY_MS = 5_000L
    }
}
