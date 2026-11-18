package dev.geode.ui

/**
 * Listen time gathered for one track. It is added to the history in memory ([addToHistory]) once
 * [memoryIntervalMs] has built up and when the track changes. The history goes to disk ([writeToDisk]) only on
 * [commit] (a track change, a pause, a stop, the app going to the background) and, as a safety, after
 * [DISK_INTERVAL_MS] of listening with no commit.
 */
internal class ListenBatch(
    private val memoryIntervalMs: Long = MEMORY_INTERVAL_MS,
    private val diskIntervalMs: Long = DISK_INTERVAL_MS,
    private val addToHistory: (uri: String, ms: Long) -> Unit,
    private val writeToDisk: () -> Unit,
) {
    private var uri: String? = null
    private var pendingMs = 0L
    private var unsavedMs = 0L

    fun add(
        uri: String,
        deltaMs: Long,
    ) {
        if (uri != this.uri) drain()
        this.uri = uri
        pendingMs += deltaMs
        unsavedMs += deltaMs
        if (pendingMs >= memoryIntervalMs) drain()
        if (unsavedMs >= diskIntervalMs) commit()
    }

    /** What is gathered goes to the history and the history goes to disk. */
    fun commit() {
        drain()
        unsavedMs = 0L
        writeToDisk()
    }

    private fun drain() {
        val u = uri
        val ms = pendingMs
        pendingMs = 0L
        if (u != null && ms > 0L) addToHistory(u, ms)
    }

    companion object {
        const val MEMORY_INTERVAL_MS = 10_000L

        /** The safety write of the history to disk while one track plays on uninterrupted. */
        const val DISK_INTERVAL_MS = 60_000L
    }
}
