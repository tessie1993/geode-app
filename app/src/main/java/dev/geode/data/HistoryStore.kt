package dev.geode.data

import android.content.Context
import dev.geode.engine.bridge.LibraryNative

/**
 * The play history's file (history.json). Every rule (which plays count, the stamps, listened time, what is kept
 * on disk, the newest-first order) is native's (core/library/History); this holds the file's text as the state,
 * hands it to native and keeps what comes back, and does the lock, the quarantine and the write queue.
 */
class HistoryStore(
    context: Context,
) {
    private val file = java.io.File(context.filesDir, "history.json")

    private val lock = Any()

    // history.json's own schema; "" is no history.
    @Volatile
    private var state = ""

    @Volatile
    private var readable = true

    data class Entry(
        val uri: String,
        val lastPlayedMs: Long,
        val playCount: Int,
        val title: String,
        val artist: String = "",
        val listenedMs: Long = 0L,
    )

    init {
        synchronized(lock) { readLocked() }
    }

    private fun readLocked() {
        if (!file.exists()) return
        val text = runCatching { file.readText() }.getOrNull()
        if (text == null) {
            readable = false
            return
        }
        if (!LibraryNative.historyCorrupt(text)) {
            state = text
            return
        }
        AtomicWrite.quarantine(file)
    }

    fun recordPlay(
        uri: String,
        title: String,
        artist: String = "",
    ) {
        synchronized(lock) {
            state = LibraryNative.historyRecordPlay(state, uri, title, artist, System.currentTimeMillis()) ?: return
        }
        persist()
    }

    fun addListenTime(
        uri: String,
        deltaMs: Long,
    ) {
        if (deltaMs <= 0) return
        synchronized(lock) {
            LibraryNative.historyAddListen(state, uri, deltaMs)?.let { state = it }
            dirty = true
        }
    }

    fun flush() {
        if (dirty) persist()
    }

    fun awaitWrites(timeoutMs: Long = AWAIT_WRITE_TIMEOUT_MS) {
        runCatching { writer.submit {}.get(timeoutMs.coerceAtLeast(0L), java.util.concurrent.TimeUnit.MILLISECONDS) }
    }

    @Volatile
    private var dirty = false

    private val writeQueued =
        java.util.concurrent.atomic
            .AtomicBoolean(false)

    fun recentlyPlayed(limit: Int = 20): List<Entry> = entriesOf(LibraryNative.historyRecent(state, limit))

    /** The history as native holds it (history.json's schema): what a smart playlist reads its play counts from. */
    fun text(): String = state

    private fun persist() {
        if (!writeQueued.compareAndSet(false, true)) return
        runCatching {
            writer.execute {
                writeQueued.set(false)
                writeNow()
            }
        }
    }

    private fun writeNow() {
        if (!readable) return
        runCatching {
            // Cleared before the state is read: a listen added after it sets the flag again and is not lost.
            dirty = false
            val text = LibraryNative.historyFileText(state)
            if (text == null || !AtomicWrite.text(file, text)) dirty = true
        }.onFailure { dirty = true }
    }

    companion object {
        private const val AWAIT_WRITE_TIMEOUT_MS = 2_000L

        private const val FIELD = '\u001F'
        private const val RECORD = '\u001E'
        private const val ENTRY_FIELDS = 6

        private val writer =
            java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                Thread(r, "geode-history").apply { isDaemon = true }
            }

        // Native's wire (`geode_history_recent`): six fields per entry, each entry ended by U+001E.
        private fun entriesOf(wire: String?): List<Entry> =
            wire
                .orEmpty()
                .split(RECORD)
                .filter { it.isNotEmpty() }
                .mapNotNull { record ->
                    val f = record.split(FIELD)
                    if (f.size != ENTRY_FIELDS) return@mapNotNull null
                    Entry(f[0], f[1].toLong(), f[2].toInt(), f[3], f[4], f[5].toLong())
                }
    }
}
