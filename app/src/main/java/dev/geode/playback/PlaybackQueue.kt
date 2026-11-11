package dev.geode.playback

import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.model.QueueTrack

object PlaybackQueue {
    const val PREV_RESTART_MS: Long = 3_000L

    const val MAX_QUEUE = 1001

    data class Window(
        val tracks: List<QueueTrack>,
        val startIndex: Int,
    )

    fun window(
        tracks: List<QueueTrack>,
        startUri: String,
    ): Window {
        val at = tracks.indexOfFirst { it.uri == startUri }
        if (at < 0) return Window(listOf(QueueTrack(startUri)), 0)
        val (slice, index) = around(tracks, at)
        return Window(slice, index)
    }

    /**
     * At most [MAX_QUEUE] of [items] around the one at [at], and where it sits in the slice. The one
     * rule for every way a queue is built: a tap in the app, a tap in Android Auto, a voice request.
     */
    fun <T> around(
        items: List<T>,
        at: Int,
    ): Pair<List<T>, Int> {
        if (items.size <= MAX_QUEUE) return items to at
        val from = (at - MAX_QUEUE / 2).coerceIn(0, items.size - MAX_QUEUE)
        return items.subList(from, from + MAX_QUEUE) to at - from
    }

    fun contextFor(
        uri: String,
        browse: List<QueueTrack>,
        deviceTracks: List<DeviceTrack>,
        libraryTracks: List<LibraryTrack>,
    ): List<QueueTrack> {
        browse.takeIf { list -> list.any { it.uri == uri } }?.let { return it }
        deviceTracks.takeIf { list -> list.any { it.uri == uri } }?.let { return it.map(::queueTrack) }
        libraryTracks.takeIf { list -> list.any { it.uri == uri } }?.let { return it.map(::queueTrack) }
        return listOf(QueueTrack(uri))
    }

    fun queueTrack(track: DeviceTrack): QueueTrack = QueueTrack(track.uri, track.title, track.artist)

    fun queueTrack(track: LibraryTrack): QueueTrack = QueueTrack(track.uri, track.title, track.artist)
}
