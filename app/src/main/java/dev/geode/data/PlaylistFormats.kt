package dev.geode.data

import dev.geode.engine.bridge.GeodeNative

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
        val records = String(GeodeNative.playlistParse(fileName.toByteArray(), bytes), Charsets.UTF_8).split(RECORD)
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

    fun resolve(
        entries: List<PlaylistEntry>,
        urisByFileName: Map<String, List<String>>,
    ): PlaylistResolution {
        val uris = ArrayList<String>(entries.size)
        val missing = ArrayList<PlaylistEntry>()
        val ambiguous = ArrayList<PlaylistEntry>()
        for (entry in entries) {
            val candidates = urisByFileName[entry.baseName.lowercase()].orEmpty()
            if (candidates.isEmpty()) {
                missing += entry
                continue
            }
            uris += candidates.first()
            if (candidates.size > 1) ambiguous += entry
        }
        return PlaylistResolution(uris, missing, ambiguous)
    }
}
