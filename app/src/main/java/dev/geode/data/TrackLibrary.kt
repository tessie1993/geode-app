package dev.geode.data

import android.content.Context
import androidx.annotation.VisibleForTesting
import dev.geode.engine.bridge.LibraryNative
import dev.geode.model.LibraryTrack
import java.io.File

/**
 * library.json, the app's own track list. The document, its identity and merge rules and every edit are native's
 * (core/library/TrackLibraryDoc); this reads and writes the file's text, sets an unreadable file aside and does
 * what native answers.
 */
class TrackLibrary(
    context: Context,
) {
    private val file = File(context.filesDir, "library.json")

    /** The library after an edit: [json] is what to write when [changed]; [corruptInput] says the old file did not read. */
    data class Edit(
        val changed: Boolean,
        val corruptInput: Boolean,
        val json: String,
        val tracks: List<LibraryTrack>,
    )

    /** One re-read title and artist of a track. */
    data class Fresh(
        val uri: String,
        val title: String,
        val artist: String,
    )

    /** The library rules, native's; a seam so a JVM test, which has no libgeode, can stand in for them. */
    interface Rules {
        fun read(text: String): Edit?

        fun add(
            text: String,
            tracks: List<LibraryTrack>,
        ): Edit?

        fun saveInfo(
            text: String,
            track: LibraryTrack,
        ): Edit?

        fun applyFresh(
            text: String,
            fresh: List<Fresh>,
        ): Edit?
    }

    fun list(): List<LibraryTrack> = synchronized(lock) { edit { rules.read(it) }?.tracks ?: emptyList() }

    fun addAll(tracks: List<LibraryTrack>): List<LibraryTrack>? = mutate { rules.add(it, tracks) }

    /** Every re-read title and artist of one batch, written once; one that matches what is held is left alone. */
    fun applyFreshTitles(fresh: List<Fresh>): List<LibraryTrack>? = mutate { rules.applyFresh(it, fresh) }

    fun updateMetadata(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): List<LibraryTrack>? =
        mutate {
            rules.saveInfo(
                it,
                LibraryTrack(
                    uri = uri,
                    title = title,
                    artist = artist,
                    album = album,
                    genre = genre,
                    year = year,
                    trackNo = trackNo,
                    comment = comment,
                ),
            )
        }

    private fun mutate(block: (String) -> Edit?): List<LibraryTrack>? {
        synchronized(lock) {
            val edit = edit(block) ?: return null
            if (edit.changed) AtomicWrite.text(file, edit.json)
            return edit.tracks
        }
    }

    /** The file's text run through [block]; null when the file cannot be read or native failed. An unreadable library is set aside. */
    private fun edit(block: (String) -> Edit?): Edit? {
        val text = if (file.exists()) runCatching { file.readText() }.getOrNull() ?: return null else ""
        val edit = block(text) ?: return null
        if (edit.corruptInput) runCatching { file.renameTo(quarantineFile()) }
        return edit
    }

    /** `library.json.corrupt`, else the first free `library.json.<n>.corrupt`: an earlier copy is never written over. */
    private fun quarantineFile(): File =
        generateSequence(1) { it + 1 }
            .map { n -> File(file.parentFile, if (n == 1) "${file.name}.corrupt" else "${file.name}.$n.corrupt") }
            .first { !it.exists() }

    private object NativeRules : Rules {
        override fun read(text: String) = TrackWire.edit(LibraryNative.trackLibraryRead(text))

        override fun add(
            text: String,
            tracks: List<LibraryTrack>,
        ) = TrackWire.edit(LibraryNative.trackLibraryAdd(text, TrackWire.cells(tracks)))

        override fun saveInfo(
            text: String,
            track: LibraryTrack,
        ) = TrackWire.edit(
            LibraryNative.trackLibrarySaveInfo(
                text,
                track.uri,
                track.title,
                track.artist,
                track.album,
                track.genre,
                track.year,
                track.trackNo,
                track.comment,
            ),
        )

        override fun applyFresh(
            text: String,
            fresh: List<Fresh>,
        ) = TrackWire.edit(
            LibraryNative.trackLibraryApplyFresh(text, fresh.flatMap { listOf(it.uri, it.title, it.artist) }.toTypedArray()),
        )
    }

    companion object {
        private val lock = Any()

        /** Native's rules; JVM tests, which cannot load libgeode, put their own in. */
        @VisibleForTesting
        internal var rules: Rules = NativeRules
    }
}
