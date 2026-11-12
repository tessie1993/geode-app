package dev.geode.playback

import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import dev.geode.data.HistoryStore
import dev.geode.data.SessionStore
import dev.geode.model.OpenedAudio
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
 * play, as the UI listener did, except a queue swapped in while the player is not set to play (a
 * restore), which counts once playback starts. The queue is walked only after a timeline change; the
 * position and index are two cheap reads. Callbacks and the tick run on the main thread, where the player lives;
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

        /** The queue's tracks (items without a uri left out), or null when the queue is empty. One pass. */
        fun queue(): List<SessionStore.SavedTrack>?

        /** The current index within the last [queue]; no walk of the queue. */
        fun index(): Int

        fun positionMs(): Long

        /** Whether the player is set to play (not whether it is yet): a restored queue waits paused. */
        fun wantsToPlay(): Boolean
    }

    private val _plays = MutableStateFlow(0)

    /** Bumps once per recorded play, so screens showing history (Recent, counts) re-read it. */
    val plays: StateFlow<Int> = _plays.asStateFlow()

    private var ticker: Job? = null

    // The queue as of the last timeline change. Positions and ticks reuse it, so only a queue edit
    // pays for the walk over every item on the main thread.
    private var queue: List<SessionStore.SavedTrack>? = null
    private var queueStale = true

    // A queue swapped in while paused is a restore, not a play: its track counts once playback starts.
    private var pendingUri: String? = null

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        pendingUri = null
        val restoreCandidate = reason == Player.MEDIA_ITEM_TRANSITION_REASON_PLAYLIST_CHANGED && !source.wantsToPlay()
        source.current()?.let { t ->
            if (restoreCandidate) pendingUri = t.uri else recordPlay(t)
        }
        saveNow()
    }

    override fun onTimelineChanged(
        timeline: Timeline,
        reason: Int,
    ) {
        queueStale = true
        saveNow()
    }

    override fun onPositionDiscontinuity(
        oldPosition: Player.PositionInfo,
        newPosition: Player.PositionInfo,
        reason: Int,
    ) = saveNow()

    override fun onIsPlayingChanged(isPlaying: Boolean) {
        if (isPlaying) {
            val pending = pendingUri
            pendingUri = null
            source.current()?.takeIf { it.uri == pending }?.let(::recordPlay)
        }
        saveNow()
        if (isPlaying) startTicker() else stopTicker()
    }

    /** Saves the final position, before the player is released. */
    fun release() {
        stopTicker()
        saveNow()
        history.flush()
    }

    private fun recordPlay(t: SessionStore.SavedTrack) {
        history.recordPlay(t.uri, t.title, t.artist)
        _plays.update { it + 1 }
    }

    private fun saveNow() {
        if (queueStale || queue == null) {
            queue = source.queue()
            queueStale = false
        }
        val tracks = queue?.takeIf { it.isNotEmpty() } ?: return
        save(SessionStore.Saved(tracks, source.index(), source.positionMs()))
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
        /**
         * [Source] over the real player: the queue's uris and metadata, as [PlaybackService] rebuilds them.
         * A file another app opened ([OpenedAudio]) is left out like an item without a uri: its grant
         * ends with the sender's task, so neither history nor resumption may keep it.
         */
        fun sourceOf(player: Player): Source =
            object : Source {
                override fun current(): SessionStore.SavedTrack? {
                    val item = player.currentMediaItem ?: return null
                    if (OpenedAudio.isOpened(item.mediaId)) return null
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

                // Raw positions of items without a uri (or opened files) in the last queue(), so index() needs no walk.
                private var dropped = IntArray(0)

                override fun wantsToPlay(): Boolean = player.playWhenReady

                override fun positionMs(): Long = player.currentPosition

                override fun index(): Int {
                    val raw = player.currentMediaItemIndex
                    return raw - dropped.count { it < raw }
                }

                override fun queue(): List<SessionStore.SavedTrack>? {
                    val count = player.mediaItemCount
                    if (count == 0) return null
                    val skipped = ArrayList<Int>()
                    val tracks =
                        (0 until count).mapNotNull { i ->
                            val item = player.getMediaItemAt(i)
                            val uri =
                                item.localConfiguration
                                    ?.uri
                                    ?.toString()
                                    ?.takeUnless { OpenedAudio.isOpened(item.mediaId) } ?: run {
                                    skipped.add(i)
                                    return@mapNotNull null
                                }
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
                    dropped = skipped.toIntArray()
                    return tracks.ifEmpty { null }
                }
            }
    }
}
