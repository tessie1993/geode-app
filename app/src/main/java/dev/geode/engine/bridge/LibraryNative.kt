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
     * session (the file is moved aside); null when native failed.
     */
    external fun sessionRead(text: String): String?

    /**
     * The text session.json is written as (`geode_session_write`): [tracks] holds three strings per track (uri,
     * title, artist); index and position come clamped. "" when there are no tracks (the file is deleted instead).
     */
    external fun sessionWrite(
        tracks: Array<String>,
        index: Int,
        positionMs: Long,
    ): String?
}
