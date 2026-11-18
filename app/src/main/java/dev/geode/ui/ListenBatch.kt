package dev.geode.ui

/**
 * Listen time gathered for one track and handed to [write] once [intervalMs] has built up, when the track
 * changes, or on [drain] (a pause, a stop).
 */
internal class ListenBatch(
    private val intervalMs: Long = DEFAULT_INTERVAL_MS,
    private val write: (uri: String, ms: Long) -> Unit,
) {
    private var uri: String? = null
    private var pendingMs = 0L

    fun add(
        uri: String,
        deltaMs: Long,
    ) {
        if (uri != this.uri) drain()
        this.uri = uri
        pendingMs += deltaMs
        if (pendingMs >= intervalMs) drain()
    }

    fun drain() {
        val u = uri
        val ms = pendingMs
        pendingMs = 0L
        if (u != null && ms > 0L) write(u, ms)
    }

    private companion object {
        const val DEFAULT_INTERVAL_MS = 10_000L
    }
}
