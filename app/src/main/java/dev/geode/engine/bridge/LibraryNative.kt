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
}
