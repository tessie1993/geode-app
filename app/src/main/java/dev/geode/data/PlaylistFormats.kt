package dev.geode.data

import dev.geode.engine.bridge.LibraryNative
import dev.geode.engine.bridge.TagsNative
import dev.geode.model.LibraryTrack

data class PlaylistEntry(
    val location: String,
    val title: String = "",
    val durationMs: Long = PlaylistFormats.UNKNOWN_DURATION,
    val baseName: String = "",
)

sealed interface PlaylistParse {
    data class Parsed(
        val name: String,
        val entries: List<PlaylistEntry>,
    ) : PlaylistParse

    data class Unreadable(
        val why: String,
    ) : PlaylistParse
}

data class PlaylistResolution(
    val uris: List<String>,
    val missing: List<PlaylistEntry>,
    val ambiguous: List<PlaylistEntry>,
)

/**
 * The shell's side of a picked M3U, PLS or XSPF file. Reading the formats is native (`core/library/Playlist`);
 * this only splits its text and matches the entries to the library by file name, which stays here because the
 * names being matched are the library's own strings.
 */
object PlaylistFormats {
    const val UNKNOWN_DURATION = -1L

    private const val RECORD = '\u001e'
    private const val FIELD = '\u001f'

    fun parse(
        fileName: String,
        bytes: ByteArray,
    ): PlaylistParse {
        val records = String(TagsNative.playlistParse(fileName.toByteArray(), bytes), Charsets.UTF_8).split(RECORD)
        val head = records.first().split(FIELD)
        if (head.first() != "O") return PlaylistParse.Unreadable(head.getOrElse(1) { "" })
        val entries =
            records.drop(1).map { record ->
                val fields = record.split(FIELD)
                PlaylistEntry(
                    location = fields[0],
                    title = fields[1],
                    durationMs = fields[2].toLong(),
                    baseName = fields[3],
                )
            }
        return PlaylistParse.Parsed(head.getOrElse(1) { "" }, entries)
    }

    /**
     * The entries matched to [library] by file name, native's rule (`core/library/TrackRules`,
     * `resolvePlaylistEntries`); null when native failed.
     */
    fun resolve(
        entries: List<PlaylistEntry>,
        library: List<LibraryTrack>,
    ): PlaylistResolution? {
        val records =
            TrackWire.records(
                LibraryNative.trackResolveEntries(
                    entries.map { it.baseName }.toTypedArray(),
                    library.flatMap { listOf(it.fileName, it.uri) }.toTypedArray(),
                ),
            )
        if (records.size < 3) return null
        val uris = records[0].filter { it.isNotEmpty() }
        val missing = records[1].mapNotNull { it.toIntOrNull()?.let(entries::getOrNull) }
        val ambiguous = records[2].mapNotNull { it.toIntOrNull()?.let(entries::getOrNull) }
        return PlaylistResolution(uris, missing, ambiguous)
    }
}
