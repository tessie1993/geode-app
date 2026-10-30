package dev.geode.ui

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.core.net.toUri
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import dev.geode.playback.MediaArtwork
import dev.geode.playback.QueueOps
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class AbLoop(
    val startMs: Long,
    val endMs: Long? = null,
)

internal class QueueController(
    private val application: Application,
    private val host: Host,
) {
    interface Host {
        val player: Player
        val libraryTracks: List<LibraryTrack>
        val deviceTracks: List<DeviceTrack>

        fun stopLiveInput()

        fun onQueueStarted(startUri: Uri)

        fun skipFaded(action: () -> Unit)

        fun refreshUi()
    }

    private val player: Player get() = host.player

    private val _queue = MutableStateFlow(QueueUiState())
    val queue: StateFlow<QueueUiState> = _queue

    private val _abLoop = MutableStateFlow<AbLoop?>(null)
    val abLoop: StateFlow<AbLoop?> = _abLoop

    private var lastBrowseContext: List<QueueTrack> = emptyList()

    // Host exposes no coroutine scope, so title resolution owns a small IO scope of its own,
    // scoped to this controller's (effectively app-scoped) lifetime.
    private val resolveScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // Bumped by every open()/playFrom() so a title resolution batch from a superseded call can
    // detect it is stale and skip applying its results.
    private var openGeneration = 0

    // Starts true so the first poll publishes whatever the process-wide player already holds.
    private var queueStale = true

    /**
     * Marks the queue stale only when the player says its contents, order or position changed.
     * It only sets a flag: a title resolution batch replaces items one by one, and rebuilding the
     * whole queue for each of those events would cost more than the poll this replaced.
     * Repeat mode is left out because [playOrder] ignores it.
     */
    private val queueListener =
        object : Player.Listener {
            override fun onEvents(
                player: Player,
                events: Player.Events,
            ) {
                val changed =
                    events.containsAny(
                        Player.EVENT_TIMELINE_CHANGED,
                        Player.EVENT_MEDIA_ITEM_TRANSITION,
                        Player.EVENT_SHUFFLE_MODE_ENABLED_CHANGED,
                    )
                if (changed) queueStale = true
            }
        }

    init {
        player.addListener(queueListener)
    }

    /** Detaches from the player, which outlives this controller's session. */
    fun release() {
        player.removeListener(queueListener)
    }

    fun playTrack(uri: String) = playFrom(PlaybackQueue.contextFor(uri, lastBrowseContext, host.deviceTracks, host.libraryTracks), uri)

    fun playFrom(
        tracks: List<QueueTrack>,
        startUri: String,
    ) {
        val window = PlaybackQueue.window(tracks, startUri)
        if (window.tracks.isEmpty()) return
        host.stopLiveInput()
        lastBrowseContext = tracks
        val generation = ++openGeneration
        val unresolved = mutableListOf<Uri>()
        val items =
            window.tracks.map { track ->
                if (track.title.isNotBlank()) {
                    mediaItemFor(track)
                } else {
                    val uri = track.uri.toUri()
                    val quick = quickMediaItem(uri)
                    if (quick.unresolved) unresolved += uri
                    quick.item
                }
            }
        player.setMediaItems(items)
        player.prepare()
        player.seekTo(window.startIndex, 0L)
        player.play()
        host.onQueueStarted(window.tracks[window.startIndex].uri.toUri())
        if (unresolved.isNotEmpty()) resolveTitlesAsync(unresolved, generation)
    }

    fun playAll(
        tracks: List<QueueTrack>,
        shuffled: Boolean = false,
    ) {
        val order = if (shuffled) tracks.shuffled() else tracks
        order.firstOrNull()?.let { playFrom(order, it.uri) }
    }

    fun open(uris: List<Uri>) {
        if (uris.isEmpty()) return
        val generation = ++openGeneration
        val quick = uris.map { quickMediaItem(it) }
        player.setMediaItems(quick.map { it.item })
        player.prepare()
        player.play()
        host.onQueueStarted(uris.first())
        val unresolved = uris.filterIndexed { i, _ -> quick[i].unresolved }
        if (unresolved.isNotEmpty()) resolveTitlesAsync(unresolved, generation)
    }

    fun playNext(uri: String) {
        val at = QueueOps.insertNextIndex(player.currentMediaItemIndex, player.mediaItemCount)
        player.addMediaItem(at, mediaItemFor(uri.toUri()))
        host.refreshUi()
    }

    fun enqueue(uri: String) {
        player.addMediaItem(mediaItemFor(uri.toUri()))
        host.refreshUi()
    }

    fun mediaItemFor(uri: Uri): MediaItem {
        val known = host.libraryTracks.firstOrNull { it.uri == uri.toString() }
        val (t, a) = if (known != null) known.title to known.artist else metadataQuick(uri)
        return mediaItemFor(uri, t, a)
    }

    private fun mediaItemFor(
        uri: Uri,
        title: String,
        artist: String = "",
    ): MediaItem =
        MediaItem
            .Builder()
            .setUri(uri)
            // Distinct per track: MediaMetadata.equals ignores extras, so without this two
            // untitled tracks compare equal and the platform session skips the metadata update.
            .setMediaId(uri.toString())
            .setMediaMetadata(
                MediaMetadata
                    .Builder()
                    .setTitle(title)
                    .setArtist(artist.ifBlank { null })
                    .setExtras(MediaArtwork.embeddedArtExtras(uri.toString()))
                    .build(),
            ).build()

    private data class QuickItem(
        val item: MediaItem,
        val unresolved: Boolean,
    )

    /**
     * Builds a queue item without touching the ContentResolver: a known library title is used
     * as-is, otherwise a placeholder (last path segment, or the uri itself) stands in until
     * [resolveTitlesAsync] patches in the real DISPLAY_NAME off the main thread.
     */
    private fun quickMediaItem(uri: Uri): QuickItem {
        val known = host.libraryTracks.firstOrNull { it.uri == uri.toString() }
        return if (known != null) {
            QuickItem(mediaItemFor(uri, known.title, known.artist), unresolved = false)
        } else {
            QuickItem(mediaItemFor(uri, placeholderTitle(uri)), unresolved = true)
        }
    }

    private fun placeholderTitle(uri: Uri): String =
        uri.lastPathSegment
            ?.substringAfterLast('/')
            ?.substringBeforeLast('.')
            ?.takeIf { it.isNotBlank() }
            ?: uri.toString()

    /**
     * Resolves DISPLAY_NAME for [uris] off the main thread, then patches the results into the
     * still-live queue. Runs on [resolveScope] (IO) rather than blocking the caller, which used
     * to run this ContentResolver query synchronously on the main thread for every item.
     */
    private fun resolveTitlesAsync(
        uris: List<Uri>,
        generation: Int,
    ) {
        resolveScope.launch {
            // Queried concurrently: sequentially this can be hundreds of ContentResolver round
            // trips for a large folder, trickling titles in one at a time over many seconds.
            val resolved = uris.map { uri -> async { uri to metadataQuick(uri) } }.awaitAll()
            withContext(Dispatchers.Main.immediate) {
                // A newer open()/playFrom() replaced the queue before this batch finished;
                // applying these titles now would land on the wrong tracks.
                if (generation != openGeneration) return@withContext
                resolved.forEach { (uri, meta) ->
                    // A duplicate uri can appear more than once in the queue; patch every match.
                    indicesOfMediaItem(uri).forEach { index ->
                        player.replaceMediaItem(index, mediaItemFor(uri, meta.first, meta.second))
                    }
                }
            }
        }
    }

    private fun indicesOfMediaItem(uri: Uri): List<Int> {
        val target = uri.toString()
        return (0 until player.mediaItemCount).filter { i -> player.getMediaItemAt(i).mediaId == target }
    }

    fun mediaItemFor(track: QueueTrack): MediaItem {
        if (track.title.isBlank()) return mediaItemFor(track.uri.toUri())
        return MediaItem
            .Builder()
            .setUri(track.uri.toUri())
            .setMediaId(track.uri)
            .setMediaMetadata(
                MediaMetadata
                    .Builder()
                    .setTitle(track.title)
                    .setArtist(track.artist.ifBlank { null })
                    .setExtras(MediaArtwork.embeddedArtExtras(track.uri))
                    .build(),
            ).build()
    }

    private fun metadataQuick(uri: Uri): Pair<String, String> {
        val name =
            runCatching {
                application.contentResolver
                    .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                    ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
            }.getOrNull()?.substringBeforeLast('.')
        return (name ?: application.getString(dev.geode.R.string.library_untitled_track)) to ""
    }

    private fun playOrder(): List<Int> {
        if (!player.shuffleModeEnabled) return (0 until player.mediaItemCount).toList()
        val timeline = player.currentTimeline
        return QueueOps.playOrder(
            count = player.mediaItemCount,
            first = timeline.getFirstWindowIndex(true),
            next = { i -> timeline.getNextWindowIndex(i, Player.REPEAT_MODE_OFF, true) },
        )
    }

    /**
     * Called by the session's 500 ms poll. It used to rebuild every [QueueTrack] on Main each tick;
     * now it rebuilds only after [queueListener] saw a change, so an idle queue costs one flag check.
     */
    fun refreshQueue() {
        if (queueStale) rebuildQueue()
    }

    private fun rebuildQueue() {
        queueStale = false
        val order = playOrder()
        val tracks =
            order.mapIndexed { position, i ->
                val item = player.getMediaItemAt(i)
                QueueTrack(
                    uri =
                        item.localConfiguration
                            ?.uri
                            ?.toString()
                            .orEmpty(),
                    title =
                        item.mediaMetadata.title?.toString()
                            ?: item.localConfiguration
                                ?.uri
                                ?.lastPathSegment
                                ?.substringAfterLast('/')
                                ?.substringBeforeLast('.')
                            ?: application.getString(dev.geode.R.string.playlist_untitled_track, position + 1),
                    artist =
                        item.mediaMetadata.artist
                            ?.toString()
                            .orEmpty(),
                )
            }
        val next = QueueUiState(tracks, order.indexOf(player.currentMediaItemIndex))
        if (next != _queue.value) _queue.value = next
    }

    fun removeQueueItem(index: Int) {
        val timelineIndex = QueueOps.timelineIndexOf(playOrder(), index)
        if (timelineIndex < 0) return
        player.removeMediaItem(timelineIndex)
        rebuildQueue()
    }

    fun moveQueueItem(
        from: Int,
        to: Int,
    ) {
        val order = playOrder()
        val timelineFrom = QueueOps.timelineIndexOf(order, from)
        val timelineTo = QueueOps.timelineIndexOf(order, to)
        if (timelineFrom < 0 || timelineTo < 0 || timelineFrom == timelineTo) return
        player.moveMediaItem(timelineFrom, timelineTo)
        rebuildQueue()
    }

    fun queueTitles(): List<String> =
        playOrder().mapIndexed { position, i ->
            val item = player.getMediaItemAt(i)
            item.mediaMetadata.title?.toString()
                ?: item.localConfiguration
                    ?.uri
                    ?.lastPathSegment
                    ?.substringAfterLast('/')
                    ?.substringBeforeLast('.')
                ?: application.getString(dev.geode.R.string.playlist_untitled_track, position + 1)
        }

    fun playQueueIndex(index: Int) {
        val timelineIndex = QueueOps.timelineIndexOf(playOrder(), index)
        if (timelineIndex >= 0) {
            player.seekTo(timelineIndex, 0L)
            player.play()
        }
    }

    fun next() {
        if (player.mediaItemCount == 0) return
        host.skipFaded {
            clearAbLoop()
            if (player.hasNextMediaItem()) player.seekToNextMediaItem() else player.seekTo(0, 0L)
        }
    }

    fun previous() {
        if (player.mediaItemCount == 0) return
        if (player.currentPosition > PlaybackQueue.PREV_RESTART_MS) {
            player.seekTo(0L)
            return
        }
        host.skipFaded {
            clearAbLoop()
            if (player.hasPreviousMediaItem()) {
                player.seekToPreviousMediaItem()
            } else {
                player.seekTo(player.mediaItemCount - 1, 0L)
            }
        }
    }

    fun seekToMs(positionMs: Long) {
        if (player.duration > 0) player.seekTo(positionMs.coerceIn(0L, player.duration))
    }

    fun seekTo(fraction: Float) {
        val d = player.duration
        if (d > 0) player.seekTo((d * fraction).toLong())
    }

    fun cycleAbLoop() {
        val at = player.currentPosition.coerceAtLeast(0)
        val loop = _abLoop.value
        _abLoop.value =
            when {
                loop == null -> AbLoop(at)
                loop.endMs == null && at > loop.startMs + MIN_LOOP_MS -> loop.copy(endMs = at)
                loop.endMs == null -> AbLoop(at)
                else -> null
            }
    }

    fun clearAbLoop() {
        _abLoop.value = null
    }

    fun enforceAbLoop() {
        val loop = _abLoop.value ?: return
        val end = loop.endMs ?: return
        if (player.currentPosition >= end) player.seekTo(loop.startMs)
    }

    private companion object {
        const val MIN_LOOP_MS = 1_000L
    }
}
