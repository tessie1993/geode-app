package dev.geode.engine.bridge

// The JNI declarations of geode_library_jni.cpp (CLAUDE.md).
object LibraryNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** True when history.json's [text] is not blank and does not read (core/library/History, `geode_history_corrupt`). */
    external fun historyCorrupt(text: String): Boolean

    /**
     * The history [state] once [uri] starts playing at [nowMs]: made if new, count up, title set, [artist] unless
     * blank, stamped after every play held (`geode_history_record_play`); null when native failed.
     */
    external fun historyRecordPlay(
        state: String,
        uri: String,
        title: String,
        artist: String,
        nowMs: Long,
    ): String?

    /** The history [state] with [deltaMs] added to [uri]'s listened time (`geode_history_add_listen`); null when native failed. */
    external fun historyAddListen(
        state: String,
        uri: String,
        deltaMs: Long,
    ): String?

    /** The text history.json is written as: the 200 most recent and the 200 most played (`geode_history_file_text`). */
    external fun historyFileText(state: String): String?

    /**
     * At most [limit] entries, newest first (`geode_history_recent`): per entry uri, last-played ms, play count,
     * title, artist and listened ms split by U+001F, each entry ended by U+001E.
     */
    external fun historyRecent(
        state: String,
        limit: Int,
    ): String?

    /**
     * The smart playlists among smart-playlists/' file [texts], sorted by lower-case name (core/library/SmartPlaylist,
     * `geode_smart_list`): per playlist its name, matchAll ("1" or "0"), limit, then each rule's field token, op token
     * and value, split by U+001F, each playlist ended by U+001E.
     */
    external fun smartList(texts: Array<String>): String?

    /**
     * The file text for a smart playlist, null for a blank [name] (`geode_smart_save_text`). [rules] holds three
     * strings per rule: field token, op token, value.
     */
    external fun smartSaveText(
        name: String,
        matchAll: Boolean,
        limit: Int,
        rules: Array<String>,
    ): String?

    /**
     * The rows of [tracks] a smart playlist picks, in order (`geode_smart_resolve`). [rules] as in [smartSaveText];
     * [tracks] holds five strings per track (uri, title, artist, album, folder), [durationsMs] and [addedSec] one
     * value per track; [history] is the play history's state (its play counts); [nowMs] the wall clock.
     */
    external fun smartResolve(
        matchAll: Boolean,
        limit: Int,
        rules: Array<String>,
        tracks: Array<String>,
        durationsMs: LongArray,
        addedSec: LongArray,
        history: String,
        favourites: Array<String>,
        nowMs: Long,
    ): IntArray

    /** The edits [musicPlaylistApply] makes, `GEODE_MUSIC_PLAYLIST_*`. */
    const val MUSIC_PLAYLIST_CREATE = 0
    const val MUSIC_PLAYLIST_IMPORT = 1
    const val MUSIC_PLAYLIST_ADD = 2
    const val MUSIC_PLAYLIST_REMOVE = 3
    const val MUSIC_PLAYLIST_MOVE = 4
    const val MUSIC_PLAYLIST_RENAME = 5

    /**
     * The playlists among music-playlists/' file [texts], sorted by lower-case name (core/library/PlaylistRules,
     * `geode_music_playlist_list`): per playlist its name, then its track uris, split by U+001F, each ended by U+001E.
     */
    external fun musicPlaylistList(texts: Array<String>): String?

    /**
     * One edit of the playlists (`geode_music_playlist_apply`), [kind] a `MUSIC_PLAYLIST_*`. [texts] are every
     * playlist file's text, [named] the text of the file [name] maps to (null when there is none, "" when it cannot
     * be read). The answer: write ("1" or "0"), ok, the name whose file to write, the name whose file to delete once
     * written, and the json, split by U+001F and ended by U+001E.
     */
    external fun musicPlaylistApply(
        kind: Int,
        name: String,
        newName: String,
        track: String,
        tracks: Array<String>,
        from: Int,
        to: Int,
        texts: Array<String>,
        named: String?,
    ): String?

    /**
     * The one-time renames of playlist files saved under an older name rule (`geode_music_playlist_migration`), given
     * every entry of the folder: [texts] holds a .json file's text, "" for anything else. Per rename its from and to
     * file names, split by U+001F, each ended by U+001E.
     */
    external fun musicPlaylistMigration(
        fileNames: Array<String>,
        texts: Array<String>,
    ): String?

    /** The name an imported file's playlist starts from (`geode_music_playlist_import_base`). */
    external fun musicPlaylistImportBase(
        parsedName: String,
        fileName: String,
        untitled: String,
    ): String?

    /**
     * The session session.json's [text] holds (core/library/SessionDoc, `geode_session_read`): one record of index,
     * position ms, then each track's uri, title and artist, split by U+001F and ended by U+001E; "" when it is no
     * session (the file is moved aside); null when native failed. [positionText] is session_position.json's text
     * ("" when there is none): native takes the position from it when it is for the same track.
     */
    external fun sessionRead(
        text: String,
        positionText: String,
    ): String?

    /** The text session_position.json is written as (`geode_session_position_write`), when only the position moved. */
    external fun sessionPositionWrite(
        index: Int,
        positionMs: Long,
    ): String?

    /**
     * The text session.json is written as (`geode_session_write`): [tracks] holds three strings per track (uri,
     * title, artist); index and position come clamped. "" when there are no tracks (the file is deleted instead).
     */
    external fun sessionWrite(
        tracks: Array<String>,
        index: Int,
        positionMs: Long,
    ): String?

    /** `GEODE_TRACK_FIELDS`: the strings one track crosses as (uri, title, artist, duration ms, album, genre, year, track no, comment, file name, size). */
    const val TRACK_FIELDS = 11

    /**
     * What library.json's [text] holds (core/library/TrackLibraryDoc, `geode_track_library_read`): a record of changed,
     * corrupt-input and json, then a record of [TRACK_FIELDS] fields per track, split by U+001F and ended by U+001E; the
     * same answer shape as the edits below. Null when native failed.
     */
    external fun trackLibraryRead(text: String): String?

    /** [text] with [tracks] ([TRACK_FIELDS] strings each) merged in and sorted (`geode_track_library_add`). */
    external fun trackLibraryAdd(
        text: String,
        tracks: Array<String>,
    ): String?

    /** [text] with [uri]'s info set, the track added when it is not held (`geode_track_library_save_info`). */
    external fun trackLibrarySaveInfo(
        text: String,
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): String?

    /** [text] with re-read titles applied one after another; [fresh] holds uri, title and artist per track. */
    external fun trackLibraryApplyFresh(
        text: String,
        fresh: Array<String>,
    ): String?

    /**
     * One track record per file (`geode_track_tag_fallbacks`). [files] holds nine strings per file: the reader's
     * title, artist, album, genre, year and track number (null for none), the uri, the display name ("" for none) and
     * the uri's last path segment (null for none); [sizes] the provider's sizes; [untitled] the localized "Untitled track".
     */
    external fun trackTagFallbacks(
        files: Array<String?>,
        sizes: LongArray,
        untitled: String,
    ): String?

    /** What `trackWalkActions` says of an entry: skip it (0), enter the folder, keep the file as audio. */
    const val WALK_DESCEND = 1
    const val WALK_KEEP_AUDIO = 2

    /** Per entry of one folder listing its action (`geode_track_walk_actions`); [parentDepth] is 0 for the picked root. */
    external fun trackWalkActions(
        names: Array<String?>,
        mimes: Array<String?>,
        isDir: IntArray,
        parentDepth: Int,
    ): IntArray

    /** Per track (a title and an artist each) 1 when its title should be read again from the file (`geode_track_title_refresh`). */
    external fun trackTitleRefresh(titleArtist: Array<String>): IntArray

    /**
     * The device rows [minSeconds] keeps (`geode_track_device_rows`): [rows] holds title, artist, album and path per
     * row (null for none), [fallbacks] the names for a missing title, artist and album. A record per kept row (index,
     * title, artist, album, folder), then the count left out.
     */
    external fun trackDeviceRows(
        rows: Array<String?>,
        durationsMs: LongArray,
        minSeconds: Int,
        fallbacks: Array<String>,
    ): String?

    /**
     * A playlist file's entries matched to the library by file name (`geode_track_resolve_entries`); [library] holds a
     * file name and a uri per track. Three records: the uris, the missing entries' indices, the ambiguous ones'.
     */
    external fun trackResolveEntries(
        baseNames: Array<String>,
        library: Array<String>,
    ): String?
}
