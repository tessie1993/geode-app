package dev.geode.data

import android.content.Context
import dev.geode.engine.bridge.LibraryNative
import java.io.File

/**
 * The saved play queue (session.json) and, beside it, where playback is in it (session_position.json). Which text
 * reads as a session and what is written for either are native's (core/library/SessionDoc); this reads and writes
 * the files and moves an unreadable one aside. The queue is written when it or the track changes; the position
 * tick that follows writes only the small position file, and nothing when neither moved.
 */
class SessionStore(
    context: Context,
) {
    private val file = File(context.filesDir, FILE_NAME)
    private val positionFile = File(context.filesDir, POSITION_FILE_NAME)

    // What the two files hold as of the last save by this store.
    private var written: Saved? = null

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
        val positionText = runCatching { if (positionFile.isFile) positionFile.readText() else "" }.getOrDefault("")
        val wire = LibraryNative.sessionRead(text, positionText) ?: return null
        val saved = savedOf(wire)
        // No session in it (garbage, a newer app's file, no track): moved aside, as a corrupt file.
        if (saved == null) AtomicWrite.quarantine(file)
        return saved
    }

    @Synchronized
    fun save(session: Saved): Boolean {
        val last = written
        if (last != null && last.tracks == session.tracks) {
            if (last.index == session.index && last.positionMs == session.positionMs) return true
            return savePosition(session)
        }
        val cells = session.tracks.flatMap { listOf(it.uri, it.title, it.artist) }.toTypedArray()
        val text = LibraryNative.sessionWrite(cells, session.index, session.positionMs) ?: return false
        if (text.isEmpty()) return clear()
        // A position left from the old queue must not outlive it, whichever write the process dies between.
        positionFile.delete()
        if (!AtomicWrite.text(file, text)) return false
        written = session.copy(positionMs = -1L)
        return savePosition(session)
    }

    private fun savePosition(session: Saved): Boolean {
        val text = LibraryNative.sessionPositionWrite(session.index, session.positionMs) ?: return false
        if (!AtomicWrite.text(positionFile, text)) return false
        written = session
        return true
    }

    @Synchronized
    fun clear(): Boolean {
        written = null
        val positionGone = !positionFile.exists() || positionFile.delete()
        val queueGone = !file.exists() || file.delete()
        return positionGone && queueGone
    }

    companion object {
        private const val FILE_NAME = "session.json"
        private const val POSITION_FILE_NAME = "session_position.json"

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
