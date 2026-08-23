package dev.geode.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

class SessionStore(
    context: Context,
) {
    private val file = File(context.filesDir, FILE_NAME)

    data class SavedTrack(
        val uri: String,
        val title: String,
        val artist: String,
    )

    data class Saved(
        val tracks: List<SavedTrack>,
        val index: Int,
        val positionMs: Long,
    )

    @Suppress("ReturnCount")
    fun load(): Saved? {
        if (!file.isFile) return null
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        val parsed =
            runCatching { parse(text) }.getOrElse {
                AtomicWrite.quarantine(file)
                return null
            }
        if (parsed == null) AtomicWrite.quarantine(file)
        return parsed
    }

    fun save(session: Saved): Boolean {
        if (session.tracks.isEmpty()) return clear()
        val items = JSONArray()
        for (t in session.tracks) {
            items.put(
                JSONObject()
                    .put(KEY_URI, t.uri)
                    .put(KEY_TITLE, t.title)
                    .put(KEY_ARTIST, t.artist),
            )
        }
        val root =
            JSONObject()
                .put(KEY_VERSION, VERSION)
                .put(KEY_TRACKS, items)
                .put(KEY_INDEX, session.index.coerceIn(0, session.tracks.size - 1))
                .put(KEY_POSITION, session.positionMs.coerceAtLeast(0L))
        return AtomicWrite.text(file, root.toString())
    }

    fun clear(): Boolean {
        if (!file.exists()) return true
        return file.delete()
    }

    @Suppress("ReturnCount", "LoopWithTooManyJumpStatements")
    private fun parse(text: String): Saved? {
        val root = JSONObject(text)
        val array = root.optJSONArray(KEY_TRACKS) ?: return null
        val tracks = ArrayList<SavedTrack>(array.length())
        for (i in 0 until array.length()) {
            val item = array.optJSONObject(i) ?: continue
            val uri = item.optString(KEY_URI).orEmpty()
            if (uri.isBlank()) continue
            tracks +=
                SavedTrack(
                    uri = uri,
                    title = item.optString(KEY_TITLE).orEmpty(),
                    artist = item.optString(KEY_ARTIST).orEmpty(),
                )
        }
        if (tracks.isEmpty()) return null
        return Saved(
            tracks = tracks,
            index = root.optInt(KEY_INDEX, 0).coerceIn(0, tracks.size - 1),
            positionMs = root.optLong(KEY_POSITION, 0L).coerceAtLeast(0L),
        )
    }

    companion object {
        private const val FILE_NAME = "session.json"

        private const val VERSION = 1

        private const val KEY_VERSION = "version"
        private const val KEY_TRACKS = "tracks"
        private const val KEY_INDEX = "index"
        private const val KEY_POSITION = "positionMs"
        private const val KEY_URI = "uri"
        private const val KEY_TITLE = "title"
        private const val KEY_ARTIST = "artist"

        const val POSITION_WRITE_INTERVAL_MS: Long = 5_000
    }
}
