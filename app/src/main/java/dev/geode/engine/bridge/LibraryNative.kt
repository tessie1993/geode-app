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

    /** [uri]'s entry in the wire of [historyRecent], "" when none (`geode_history_entry`). */
    external fun historyEntry(
        state: String,
        uri: String,
    ): String?
}
