package dev.geode.ui

import android.content.Context
import dev.geode.data.AtomicWrite
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class LibraryTrack(
    val uri: String,
    val title: String,
    val artist: String = "",
    val durationMs: Long = 0L,
    val bpm: Float = 0f,
    val key: String = "",
    val analyzed: Boolean = false,
    val album: String = "",
    val genre: String = "",
    val year: Int = 0,
    val trackNo: Int = 0,
    val comment: String = "",
    val fileName: String = "",
    val sizeBytes: Long = 0L,
)

class TrackLibrary(
    context: Context,
) {
    private val file = File(context.filesDir, "library.json")

    fun list(): List<LibraryTrack> = synchronized(lock) { readLocked() ?: emptyList() }

    fun addAll(tracks: List<LibraryTrack>): List<LibraryTrack>? = mutate { current -> mergeAdds(current, tracks) }

    fun updateAnalysis(
        uri: String,
        title: String,
        durationMs: Long,
        bpm: Float,
        key: String = "",
    ): List<LibraryTrack>? =
        mutate { current ->
            val map = current.associateBy { it.uri }.toMutableMap()
            val base = map[uri] ?: LibraryTrack(uri = uri, title = title)
            map[uri] =
                base.copy(
                    durationMs = if (durationMs > 0) durationMs else base.durationMs,
                    bpm = bpm,
                    key = key.ifBlank { base.key },
                    analyzed = true,
                )
            map.values.sortedBy { it.title.lowercase() }
        }

    fun updateMetadata(
        uri: String,
        title: String,
        artist: String,
    ): List<LibraryTrack>? =
        mutate { current ->
            val prev = current.firstOrNull { it.uri == uri } ?: return@mutate current
            upsertInfo(current, uri, title, artist, prev.album, prev.genre, prev.year, prev.trackNo, prev.comment)
        }

    fun updateMetadata(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): List<LibraryTrack>? = mutate { upsertInfo(it, uri, title, artist, album, genre, year, trackNo, comment) }

    fun remove(uri: String): List<LibraryTrack>? = mutate { current -> current.filterNot { it.uri == uri } }

    private fun mutate(block: (List<LibraryTrack>) -> List<LibraryTrack>): List<LibraryTrack>? {
        synchronized(lock) {
            val current = readLocked() ?: return null
            val merged = block(current)
            if (merged !== current) writeLocked(merged)
            return merged
        }
    }

    private fun readLocked(): List<LibraryTrack>? {
        if (!file.exists()) return emptyList()
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        if (text.isBlank()) return emptyList()
        parseOrNull(text)?.let { return it }
        runCatching { file.renameTo(File(file.parentFile, "${file.name}.corrupt")) }
        return emptyList()
    }

    private fun writeLocked(tracks: List<LibraryTrack>) {
        AtomicWrite.text(file, serialize(tracks))
    }

    companion object {
        private const val VERSION = 3

        private val lock = Any()

        internal fun identityKey(t: LibraryTrack): String =
            if (t.fileName.isNotBlank() && t.sizeBytes > 0L) {
                "file:${t.fileName.lowercase()}:${t.sizeBytes}"
            } else {
                "uri:${t.uri}"
            }

        internal fun mergeAdds(
            existing: List<LibraryTrack>,
            incoming: List<LibraryTrack>,
        ): List<LibraryTrack> {
            val identityByUri =
                incoming
                    .filter { it.fileName.isNotBlank() && it.sizeBytes > 0L }
                    .associateBy { it.uri }
            val merged = LinkedHashMap<String, LibraryTrack>()

            fun keep(t: LibraryTrack) {
                val key = identityKey(t)
                val held = merged[key]
                merged[key] = if (held == null) t else richer(held, t)
            }
            for (t in existing) {
                val id = if (t.fileName.isBlank()) identityByUri[t.uri] else null
                keep(if (id == null) t else t.copy(fileName = id.fileName, sizeBytes = id.sizeBytes))
            }
            for (t in incoming) keep(t)
            return merged.values.sortedBy { it.title.lowercase() }
        }

        private fun richer(
            kept: LibraryTrack,
            other: LibraryTrack,
        ): LibraryTrack = if (!kept.analyzed && other.analyzed) other else kept

        internal fun upsertInfo(
            tracks: List<LibraryTrack>,
            uri: String,
            title: String,
            artist: String,
            album: String,
            genre: String,
            year: Int,
            trackNo: Int,
            comment: String,
        ): List<LibraryTrack> {
            val map = tracks.associateBy { it.uri }.toMutableMap()
            val base = map[uri] ?: LibraryTrack(uri = uri, title = title)
            map[uri] =
                base.copy(
                    title = title,
                    artist = artist,
                    album = album,
                    genre = genre,
                    year = year,
                    trackNo = trackNo,
                    comment = comment,
                )
            return map.values.sortedBy { it.title.lowercase() }
        }

        internal fun parse(json: String): List<LibraryTrack> = parseOrNull(json) ?: emptyList()

        internal fun parseOrNull(json: String): List<LibraryTrack>? =
            runCatching {
                val trimmed = json.trim()
                val arr = if (trimmed.startsWith("[")) JSONArray(trimmed) else JSONObject(trimmed).getJSONArray("tracks")
                (0 until arr.length()).map { fromJson(arr.getJSONObject(it)) }
            }.getOrNull()

        internal fun serialize(tracks: List<LibraryTrack>): String {
            val arr = JSONArray()
            tracks.forEach { arr.put(toJson(it)) }
            return JSONObject().put("version", VERSION).put("tracks", arr).toString()
        }

        private fun toJson(t: LibraryTrack): JSONObject =
            JSONObject()
                .put("uri", t.uri)
                .put("title", t.title)
                .put("artist", t.artist)
                .put("durationMs", t.durationMs)
                .put("bpm", t.bpm.toDouble())
                .put("key", t.key)
                .put("analyzed", t.analyzed)
                .put("album", t.album)
                .put("genre", t.genre)
                .put("year", t.year)
                .put("trackNo", t.trackNo)
                .put("comment", t.comment)
                .put("fileName", t.fileName)
                .put("sizeBytes", t.sizeBytes)

        private fun fromJson(o: JSONObject): LibraryTrack =
            LibraryTrack(
                uri = o.getString("uri"),
                title = o.optString("title", "Track"),
                artist = o.optString("artist", ""),
                durationMs = o.optLong("durationMs", 0L),
                bpm = o.optDouble("bpm", 0.0).toFloat(),
                key = o.optString("key", ""),
                analyzed = o.optBoolean("analyzed", false),
                album = o.optString("album", ""),
                genre = o.optString("genre", ""),
                year = o.optInt("year", 0),
                trackNo = o.optInt("trackNo", 0),
                comment = o.optString("comment", ""),
                fileName = o.optString("fileName", ""),
                sizeBytes = o.optLong("sizeBytes", 0L),
            )
    }
}
