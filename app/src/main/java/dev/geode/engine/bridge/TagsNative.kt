package dev.geode.engine.bridge

// The JNI declarations of geode_tags_jni.cpp (CLAUDE.md).
object TagsNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * Reads the tags of [fd], which native closes. Fills [texts] with UTF-8 title, artist, album, album artist,
     * genre, comment; [ints] with year, track, duration ms, art bytes; [gains] with track gain dB, track peak,
     * album gain dB, album peak. Returns the mask of gains present (1, 2, 4, 8), -1 when unreadable.
     */
    external fun tagsRead(
        fd: Int,
        texts: Array<ByteArray?>,
        ints: IntArray,
        gains: FloatArray,
    ): Int

    /**
     * Writes the six UTF-8 [texts] plus [year] and [track] to [fd], which native closes. A missing array
     * slot leaves that field unchanged.
     */
    external fun tagsWrite(
        fd: Int,
        texts: Array<ByteArray>,
        year: Int,
        track: Int,
    ): Boolean

    /**
     * Voice "play X" and Android Auto's search over the library (core/library/Search), best first. [rows]
     * holds three UTF-8 strings per track (title, artist, album) in library order; [request] the query,
     * then MediaStore's focus, artist, album and title extras (null = absent). Writes matching track indices into [out] and returns how many
     * match in all; nothing asked matches every track, 0 means no match.
     */
    external fun librarySearch(
        rows: Array<ByteArray?>,
        request: Array<ByteArray?>,
        out: IntArray,
    ): Int

    /**
     * The Library screen's index (core/library/Search): [rows] as in [librarySearch], one duration and one
     * added time per track. Built once per library change; 0 when it cannot be built. Freed with
     * [libraryIndexDestroy].
     */
    external fun libraryIndexCreate(
        rows: Array<ByteArray?>,
        durationsMs: LongArray,
        addedSec: LongArray,
    ): Long

    external fun libraryIndexDestroy(handle: Long)

    /** Every row of the index in [sort] order: 0 title, 1 artist, 2 album, 3 duration, 4 newest first. */
    external fun libraryIndexOrder(
        handle: Long,
        sort: Int,
    ): IntArray

    /**
     * What a UTF-8 [query] finds, best first: three counts (tracks, albums, artists), then those rows, an
     * album or artist as its first row. A blank query finds nothing.
     */
    external fun libraryIndexSearch(
        handle: Long,
        query: ByteArray,
    ): IntArray

    /**
     * The global search sheet (core/library/Search): [tracks] holds five UTF-8 cells per track (title, artist,
     * album, one more field, and the URI every copy of a song shares), [preferred] marks the copy shown when
     * copies meet, [playlists] and [presets] are names. Returns three counts (tracks, playlists, presets), then
     * those rows, best first. A blank [query] finds nothing.
     */
    external fun searchSheet(
        tracks: Array<ByteArray?>,
        preferred: BooleanArray,
        playlists: Array<ByteArray?>,
        presets: Array<ByteArray?>,
        query: ByteArray,
    ): IntArray

    /**
     * A playlist file the user picked (core/library/Playlist, `geode_playlist_parse`): its name and bytes in,
     * the text out. Record 0 is `O`, name or `E`, reason; then one record per entry: location, title, duration
     * in ms (-1 unknown) and the file name inside the location. Fields are split by U+001F, records by U+001E.
     */
    external fun playlistParse(
        fileName: ByteArray,
        data: ByteArray,
    ): ByteArray

    /**
     * An .lrc or plain lyrics (UTF-8 [text]) as core/library/Lyrics' wire form: "S" or "P" and a newline, then one
     * "<timeMs>\t<text>\n" per line; empty when there are no lyrics.
     */
    external fun lyricsParse(text: ByteArray): ByteArray

    /** The line sung at [positionMs] among ascending [timesMs]; -1 before the first, with none, or when plain. */
    external fun lyricsIndexAt(
        timesMs: LongArray,
        positionMs: Long,
    ): Int
}
