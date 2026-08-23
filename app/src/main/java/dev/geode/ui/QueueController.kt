package dev.geode.ui

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import dev.geode.playback.QueueOps
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class AbLoop(
    val startMs: Long,
    val endMs: Long? = null,
) {
    val armed: Boolean get() = endMs != null
}

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

    fun playTrack(uri: String) = playFrom(PlaybackQueue.contextFor(uri, lastBrowseContext, host.deviceTracks, host.libraryTracks), uri)

    fun playFrom(
        tracks: List<QueueTrack>,
        startUri: String,
    ) {
        val window = PlaybackQueue.window(tracks, startUri)
        if (window.tracks.isEmpty()) return
        host.stopLiveInput()
        lastBrowseContext = tracks
        player.setMediaItems(window.tracks.map { mediaItemFor(it) })
        player.prepare()
        player.seekTo(window.startIndex, 0L)
        player.play()
        host.onQueueStarted(Uri.parse(window.tracks[window.startIndex].uri))
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
        player.setMediaItems(uris.map { mediaItemFor(it) })
        player.prepare()
        player.play()
        host.onQueueStarted(uris.first())
    }

    fun playNext(uri: String) {
        val at = QueueOps.insertNextIndex(player.currentMediaItemIndex, player.mediaItemCount)
        player.addMediaItem(at, mediaItemFor(Uri.parse(uri)))
        host.refreshUi()
    }

    fun enqueue(uri: String) {
        player.addMediaItem(mediaItemFor(Uri.parse(uri)))
        host.refreshUi()
    }

    fun mediaItemFor(uri: Uri): MediaItem {
        val known = host.libraryTracks.firstOrNull { it.uri == uri.toString() }
        val (t, a) = if (known != null) known.title to known.artist else metadataQuick(uri)
        return MediaItem
            .Builder()
            .setUri(uri)
            .setMediaMetadata(
                MediaMetadata
                    .Builder()
                    .setTitle(t)
                    .setArtist(a.ifBlank { null })
                    .setArtworkUri(uri)
                    .build(),
            ).build()
    }

    fun mediaItemFor(track: QueueTrack): MediaItem {
        if (track.title.isBlank()) return mediaItemFor(Uri.parse(track.uri))
        return MediaItem
            .Builder()
            .setUri(Uri.parse(track.uri))
            .setMediaMetadata(
                MediaMetadata
                    .Builder()
                    .setTitle(track.title)
                    .setArtist(track.artist.ifBlank { null })
                    .setArtworkUri(Uri.parse(track.uri))
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
        return (name ?: "Track") to ""
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

    fun refreshQueue() {
        val order = playOrder()
        val tracks =
            order.mapIndexed { position, i ->
                val item = player.getMediaItemAt(i)
                QueueTrack(
                    uri = item.localConfiguration?.uri?.toString().orEmpty(),
                    title =
                        item.mediaMetadata.title?.toString()
                            ?: item.localConfiguration
                                ?.uri
                                ?.lastPathSegment
                                ?.substringAfterLast('/')
                                ?.substringBeforeLast('.')
                            ?: "Track ${position + 1}",
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
        refreshQueue()
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
        refreshQueue()
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
                ?: "Track ${position + 1}"
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
