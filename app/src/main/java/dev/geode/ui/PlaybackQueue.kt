package dev.geode.ui

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
        if (tracks.size <= MAX_QUEUE) return Window(tracks, at)
        val half = MAX_QUEUE / 2
        val from = (at - half).coerceIn(0, tracks.size - MAX_QUEUE)
        return Window(tracks.subList(from, from + MAX_QUEUE), at - from)
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
