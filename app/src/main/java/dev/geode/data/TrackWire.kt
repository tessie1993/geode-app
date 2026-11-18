package dev.geode.data

import dev.geode.engine.bridge.LibraryNative
import dev.geode.model.LibraryTrack

/**
 * The shell's side of `LibraryNative`'s track calls: tracks go in as [LibraryNative.TRACK_FIELDS] strings each and
 * come back as records of fields split by U+001F, each record ended by U+001E. Only the packing is here; every rule
 * is native's (core/library/TrackRules, TrackLibraryDoc).
 */
internal object TrackWire {
    private const val FIELD = '\u001F'
    private const val RECORD = '\u001E'

    /** Every record is ended by one, so the last piece is empty; a record whose last field is empty lost it. */
    fun records(wire: String?): List<List<String>> =
        wire
            .orEmpty()
            .split(RECORD)
            .dropLast(1)
            .map { it.split(FIELD) }

    fun cells(tracks: List<LibraryTrack>): Array<String> =
        tracks
            .flatMap {
                listOf(
                    it.uri,
                    it.title,
                    it.artist,
                    it.durationMs.toString(),
                    it.album,
                    it.genre,
                    it.year.toString(),
                    it.trackNo.toString(),
                    it.comment,
                    it.fileName,
                    it.sizeBytes.toString(),
                )
            }.toTypedArray()

    private fun track(fields: List<String>): LibraryTrack =
        LibraryTrack(
            uri = fields[0],
            title = fields[1],
            artist = fields[2],
            durationMs = fields[3].toLongOrNull() ?: 0L,
            album = fields[4],
            genre = fields[5],
            year = fields[6].toIntOrNull() ?: 0,
            trackNo = fields[7].toIntOrNull() ?: 0,
            comment = fields[8],
            fileName = fields[9],
            sizeBytes = fields[10].toLongOrNull() ?: 0L,
        )

    /** The tracks of an answer made only of track records (`trackTagFallbacks`). */
    fun tracks(wire: String?): List<LibraryTrack> = records(wire).filter { it.size == LibraryNative.TRACK_FIELDS }.map(::track)

    /** A library edit's answer: the flags and json first, then the library after the edit; null when native failed. */
    fun edit(wire: String?): TrackLibrary.Edit? {
        val records = records(wire)
        val head = records.firstOrNull()?.takeIf { it.size >= 2 } ?: return null
        return TrackLibrary.Edit(
            changed = head[0] == "1",
            corruptInput = head[1] == "1",
            json = head.getOrElse(2) { "" },
            tracks = records.drop(1).filter { it.size == LibraryNative.TRACK_FIELDS }.map(::track),
        )
    }
}
