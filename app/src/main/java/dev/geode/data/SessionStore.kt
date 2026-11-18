package dev.geode.data

import android.content.Context
import dev.geode.engine.bridge.LibraryNative
import java.io.File

/**
 * The saved play queue (session.json). Which text reads as a session and what is written for one are native's
 * (core/library/SessionDoc); this reads and writes the file and moves an unreadable one aside.
 */
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

    fun load(): Saved? {
        if (!file.isFile) return null
        val text = runCatching { file.readText() }.getOrNull() ?: return null
        val wire = LibraryNative.sessionRead(text) ?: return null
        val saved = savedOf(wire)
        // No session in it (garbage, a newer app's file, no track): moved aside, as a corrupt file.
        if (saved == null) AtomicWrite.quarantine(file)
        return saved
    }

    fun save(session: Saved): Boolean {
        val cells = session.tracks.flatMap { listOf(it.uri, it.title, it.artist) }.toTypedArray()
        val text = LibraryNative.sessionWrite(cells, session.index, session.positionMs) ?: return false
        if (text.isEmpty()) return clear()
        return AtomicWrite.text(file, text)
    }

    fun clear(): Boolean {
        if (!file.exists()) return true
        return file.delete()
    }

    companion object {
        private const val FILE_NAME = "session.json"

        private const val FIELD = '\u001F'
        private const val RECORD = '\u001E'
        private const val HEAD_FIELDS = 2
        private const val TRACK_FIELDS = 3

        const val POSITION_WRITE_INTERVAL_MS: Long = 5_000

        // Native's wire (`geode_session_read`): index, position, then three fields per track; "" is no session.
        private fun savedOf(wire: String): Saved? {
            val f = wire.removeSuffix(RECORD.toString()).split(FIELD)
            if (f.size < HEAD_FIELDS + TRACK_FIELDS || (f.size - HEAD_FIELDS) % TRACK_FIELDS != 0) return null
            val tracks = f.drop(HEAD_FIELDS).chunked(TRACK_FIELDS).map { (uri, title, artist) -> SavedTrack(uri, title, artist) }
            return Saved(tracks, f[0].toInt(), f[1].toLong())
        }
    }
}
