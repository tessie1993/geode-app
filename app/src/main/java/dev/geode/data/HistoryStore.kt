package dev.geode.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

class HistoryStore(
    context: Context,
) {
    private val file = java.io.File(context.filesDir, "history.json")

    private val lock = Any()

    private val entries = LinkedHashMap<String, Entry>()

    @Volatile
    private var readable = true

    data class Entry(
        val uri: String,
        var lastPlayedMs: Long,
        var playCount: Int,
        var title: String,
        var artist: String = "",
        var listenedMs: Long = 0L,
    )

    /**
     * The file is read and parsed on [writer], not in the constructor: [dev.geode.playback.PlaybackSession]
     * builds this on Main the first time the UI or the service acquires the player, and the JSON parse of a
     * few hundred plays was disk I/O plus a parse on that thread at cold start. Every reader waits for it
     * ([awaitLoad]), normally long finished by then; every write is queued on the same thread after it.
     */
    private val loaded = writer.submit { synchronized(lock) { readLocked() } }

    private fun awaitLoad() {
        runCatching { loaded.get() }
    }

    private fun readLocked() {
        if (!file.exists()) return
        val text = runCatching { file.readText() }.getOrNull()
        if (text == null) {
            readable = false
            return
        }
        if (text.isBlank()) return
        if (runCatching { load(text) }.isSuccess) return
        entries.clear()
        AtomicWrite.quarantine(file)
    }

    private fun load(text: String) {
        if (text.trimStart().startsWith("[")) {
            readEntries(JSONArray(text))
            return
        }
        readEntries(JSONObject(text).optJSONArray("tracks") ?: JSONArray())
    }

    private fun readEntries(arr: JSONArray) {
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i)
            if (o == null || o.optString("uri").isBlank()) continue
            val e =
                Entry(
                    uri = o.optString("uri"),
                    lastPlayedMs = o.optLong("last"),
                    playCount = o.optInt("count"),
                    title = o.optString("title"),
                    artist = o.optString("artist"),
                    listenedMs = o.optLong("ms", 0L),
                )
            entries[e.uri] = e
        }
    }

    fun recordPlay(
        uri: String,
        title: String,
        artist: String = "",
    ) {
        awaitLoad()
        synchronized(lock) {
            val e = entries.getOrPut(uri) { Entry(uri, 0L, 0, title) }
            e.lastPlayedMs = maxOf(System.currentTimeMillis(), newestStamp() + 1)
            e.playCount++
            e.title = title
            if (artist.isNotBlank()) e.artist = artist
        }
        persist()
    }

    fun addListenTime(
        uri: String,
        deltaMs: Long,
    ) {
        if (deltaMs <= 0) return
        awaitLoad()
        synchronized(lock) {
            entries[uri]?.let { it.listenedMs += deltaMs }
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

    private fun newestStamp(): Long = entries.values.maxOfOrNull { it.lastPlayedMs } ?: 0L

    fun recentlyPlayed(limit: Int = 20): List<Entry> {
        awaitLoad()
        return synchronized(lock) { entries.values.sortedByDescending { it.lastPlayedMs }.take(limit) }
    }

    fun entryFor(uri: String): Entry? {
        awaitLoad()
        return synchronized(lock) { entries[uri] }
    }

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
            val text =
                synchronized(lock) {
                    val arr = JSONArray()
                    // Persist the union of the 200 most recent and the 200 most-played entries
                    // (deduped by uri) so a heavily played old track isn't dropped from disk even
                    // though the recency cut alone would.
                    val toPersist = LinkedHashMap<String, Entry>()
                    entries.values
                        .sortedByDescending { it.lastPlayedMs }
                        .take(200)
                        .forEach { toPersist[it.uri] = it }
                    entries.values
                        .filter { it.playCount > 0 }
                        .sortedWith(compareByDescending<Entry> { it.playCount }.thenByDescending { it.listenedMs })
                        .take(200)
                        .forEach { toPersist[it.uri] = it }
                    toPersist.values.forEach { e ->
                        arr.put(
                            JSONObject()
                                .put("uri", e.uri)
                                .put("last", e.lastPlayedMs)
                                .put("count", e.playCount)
                                .put("title", e.title)
                                .put("artist", e.artist)
                                .put("ms", e.listenedMs),
                        )
                    }
                    JSONObject().put("tracks", arr).toString()
                }
            if (AtomicWrite.text(file, text)) dirty = false
        }
    }

    companion object {
        private const val AWAIT_WRITE_TIMEOUT_MS = 2_000L

        private val writer =
            java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                Thread(r, "geode-history").apply { isDaemon = true }
            }
    }
}
