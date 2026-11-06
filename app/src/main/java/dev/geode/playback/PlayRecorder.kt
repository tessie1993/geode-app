package dev.geode.playback

import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import dev.geode.data.HistoryStore
import dev.geode.data.SessionStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Writes what playback resumption and play history need, from the player's own events, for as long
 * as the player lives (#371).
 *
 * It used to be the UI session's poll loop that saved the queue and position ([SessionStore]) and its
 * listener that recorded each play ([HistoryStore]). [PlaybackService] outlives that session: press Back
 * while music plays and the session shuts down while the service keeps playing through the notification,
 * a headset or Android Auto. From then on nothing was written, so after process death
 * [PlaybackService.onPlaybackResumption] rebuilt a stale queue and position, and Recent, history and the
 * Library's play counts missed every track played since the UI closed. [PlaybackSession] owns the one
 * player in both cases, so it owns this listener, and the UI only reads the two stores.
 *
 * Media3 leaves storing the playlist and position to the app; this is that store's writer, and it is
 * glue: an event saves, nothing decides. The queue is saved on every transition and timeline change
 * (queue edits, also while paused), the position on every pause and seek, and on a coarse tick while
 * playing so a process killed mid-track resumes near where it died. A transition to an item records a
 * play, as the UI listener did. Callbacks and the tick run on the main thread, where the player lives;
 * [save] does the disk write elsewhere.
 */
class PlayRecorder(
    private val source: Source,
    private val history: HistoryStore,
    private val scope: CoroutineScope,
    private val save: (SessionStore.Saved) -> Unit,
) : Player.Listener {
    /** The player's state, read on the main thread; a seam so a test needs no ExoPlayer. */
    interface Source {
        /** The track now current, or null when the queue is empty or the item has no uri. */
        fun current(): SessionStore.SavedTrack?

        /** The queue, the current index and position, or null when the queue is empty. */
        fun snapshot(): SessionStore.Saved?
    }

    private val _plays = MutableStateFlow(0)

    /** Bumps once per recorded play, so screens showing history (Recent, counts) re-read it. */
    val plays: StateFlow<Int> = _plays.asStateFlow()

    private var ticker: Job? = null

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        source.current()?.let { t ->
            history.recordPlay(t.uri, t.title, t.artist)
            _plays.update { it + 1 }
        }
        saveNow()
    }

    override fun onTimelineChanged(
        timeline: Timeline,
        reason: Int,
    ) = saveNow()

    override fun onPositionDiscontinuity(
        oldPosition: Player.PositionInfo,
        newPosition: Player.PositionInfo,
        reason: Int,
    ) = saveNow()

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        saveNow()
        if (isPlaying) startTicker() else stopTicker()
    }

    /** Saves the final position, before the player is released. */
    fun release() {
        stopTicker()
        saveNow()
        history.flush()
    }

    private fun saveNow() {
        source.snapshot()?.let(save)
    }

    private fun startTicker() {
        if (ticker?.isActive == true) return
        ticker =
            scope.launch {
                while (isActive) {
                    delay(SessionStore.POSITION_WRITE_INTERVAL_MS)
                    saveNow()
                }
            }
    }

    private fun stopTicker() {
        ticker?.cancel()
        ticker = null
    }

    companion object {
        /** [Source] over the real player: the queue's uris and metadata, as [PlaybackService] rebuilds them. */
        fun sourceOf(player: Player): Source =
            object : Source {
                override fun current(): SessionStore.SavedTrack? {
                    val item = player.currentMediaItem ?: return null
                    val uri = item.localConfiguration?.uri ?: return null
                    return SessionStore.SavedTrack(
                        uri = uri.toString(),
                        title = player.mediaMetadata.title?.toString() ?: uri.lastPathSegment.orEmpty(),
                        artist =
                            player.mediaMetadata.artist
                                ?.toString()
                                .orEmpty(),
                    )
                }

                override fun snapshot(): SessionStore.Saved? {
                    val count = player.mediaItemCount
                    if (count == 0) return null
                    val tracks =
                        (0 until count).mapNotNull { i ->
                            val item = player.getMediaItemAt(i)
                            val uri = item.localConfiguration?.uri?.toString() ?: return@mapNotNull null
                            SessionStore.SavedTrack(
                                uri = uri,
                                title =
                                    item.mediaMetadata.title
                                        ?.toString()
                                        .orEmpty(),
                                artist =
                                    item.mediaMetadata.artist
                                        ?.toString()
                                        .orEmpty(),
                            )
                        }
                    if (tracks.isEmpty()) return null
                    // Items without a URI were dropped above, so the index counts only the kept ones before it.
                    val index = (0 until player.currentMediaItemIndex).count { player.getMediaItemAt(it).localConfiguration != null }
                    return SessionStore.Saved(tracks, index, player.currentPosition)
                }
            }
    }
}
