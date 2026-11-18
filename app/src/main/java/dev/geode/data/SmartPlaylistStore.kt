package dev.geode.data

import android.content.Context
import dev.geode.engine.bridge.LibraryNative
import dev.geode.model.DeviceTrack
import java.io.File

/** A smart playlist rule's field; the names are the tokens the files hold (core/library/SmartPlaylist). */
enum class RuleField {
    TITLE,
    ARTIST,
    ALBUM,
    FOLDER,
    DURATION_MINUTES,
    ADDED_DAYS_AGO,
    PLAY_COUNT,
    FAVOURITE,
    ;

    val isText: Boolean get() = this == TITLE || this == ARTIST || this == ALBUM || this == FOLDER

    val isFlag: Boolean get() = this == FAVOURITE
}

/** A smart playlist rule's op; the names are the tokens the files hold. */
enum class RuleOp {
    CONTAINS,
    IS,
    IS_NOT,
    AT_LEAST,
    AT_MOST,
    ;

    val forText: Boolean get() = this == CONTAINS || this == IS || this == IS_NOT

    val forNumber: Boolean get() = this == IS || this == AT_LEAST || this == AT_MOST
}

data class SmartRule(
    val field: RuleField,
    val op: RuleOp,
    val value: String,
)

data class SmartPlaylist(
    val name: String,
    val rules: List<SmartRule> = emptyList(),
    val matchAll: Boolean = true,
    val limit: Int = 0,
)

/**
 * One JSON file per smart playlist under `files/smart-playlists`, like the ordinary playlists. The file format, the
 * order and which tracks a rule set picks are native's (core/library/SmartPlaylist); this lists and reads the files.
 */
class SmartPlaylistStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "smart-playlists").apply { mkdirs() }

    fun list(): List<SmartPlaylist> {
        val texts =
            dir
                .listFiles { f -> f.extension == "json" }
                ?.mapNotNull { runCatching { it.readText() }.getOrNull() }
                .orEmpty()
        return playlistsOf(LibraryNative.smartList(texts.toTypedArray()))
    }

    fun save(playlist: SmartPlaylist): Boolean {
        val text = LibraryNative.smartSaveText(playlist.name, playlist.matchAll, playlist.limit, cellsOf(playlist)) ?: return false
        return AtomicWrite.text(fileOf(playlist.name), text)
    }

    fun delete(name: String) {
        fileOf(name).delete()
    }

    private fun fileOf(name: String): File = File(dir, PresetStore.safeFileName(name) + ".json")

    companion object {
        private const val FIELD = '\u001F'
        private const val RECORD = '\u001E'
        private const val HEAD_FIELDS = 3
        private const val RULE_FIELDS = 3
        private const val TRACK_CELLS = 5

        /**
         * The tracks [playlist] picks from [tracks], in their order (native, `geode_smart_resolve`): [history] is the
         * play history's state ([HistoryStore.text]), which holds the play counts.
         */
        fun resolve(
            playlist: SmartPlaylist,
            tracks: List<DeviceTrack>,
            history: String,
            favourites: Set<String>,
            nowMs: Long = System.currentTimeMillis(),
        ): List<DeviceTrack> {
            val cells =
                Array(tracks.size * TRACK_CELLS) { i ->
                    val t = tracks[i / TRACK_CELLS]
                    when (i % TRACK_CELLS) {
                        0 -> t.uri
                        1 -> t.title
                        2 -> t.artist
                        3 -> t.album
                        else -> t.folder
                    }
                }
            val rows =
                LibraryNative.smartResolve(
                    playlist.matchAll,
                    playlist.limit,
                    cellsOf(playlist),
                    cells,
                    LongArray(tracks.size) { tracks[it].durationMs },
                    LongArray(tracks.size) { tracks[it].addedSec },
                    history,
                    favourites.toTypedArray(),
                    nowMs,
                )
            return rows.map { tracks[it] }
        }

        // Three strings per rule: field token, op token, value.
        private fun cellsOf(playlist: SmartPlaylist): Array<String> =
            playlist.rules.flatMap { listOf(it.field.name, it.op.name, it.value) }.toTypedArray()

        // Native's wire (`geode_smart_list`); a token this build has no entry for drops only its rule.
        private fun playlistsOf(wire: String?): List<SmartPlaylist> =
            wire
                .orEmpty()
                .split(RECORD)
                .filter { it.isNotEmpty() }
                .mapNotNull { record ->
                    val f = record.split(FIELD)
                    if (f.size < HEAD_FIELDS || (f.size - HEAD_FIELDS) % RULE_FIELDS != 0) return@mapNotNull null
                    val rules =
                        f.drop(HEAD_FIELDS).chunked(RULE_FIELDS).mapNotNull { (field, op, value) ->
                            val ruleField = RuleField.entries.firstOrNull { it.name == field } ?: return@mapNotNull null
                            val ruleOp = RuleOp.entries.firstOrNull { it.name == op } ?: return@mapNotNull null
                            SmartRule(ruleField, ruleOp, value)
                        }
                    SmartPlaylist(name = f[0], rules = rules, matchAll = f[1] == "1", limit = f[2].toInt())
                }
    }
}
