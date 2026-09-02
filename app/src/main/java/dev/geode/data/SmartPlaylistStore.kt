package dev.geode.data

import android.content.Context
import dev.geode.ui.DeviceTrack
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

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

/** Which of the library's tracks a rule set picks; the sources the rules cannot read from a track are passed in. */
object SmartPlaylistMatcher {
    fun resolve(
        playlist: SmartPlaylist,
        tracks: List<DeviceTrack>,
        playCountOf: (String) -> Int,
        favourites: Set<String>,
        nowMs: Long = System.currentTimeMillis(),
    ): List<DeviceTrack> {
        val rules = playlist.rules.filter { it.value.isNotBlank() || it.field.isFlag }
        val picked =
            if (rules.isEmpty()) {
                tracks
            } else {
                tracks.filter { track ->
                    val hits = rules.map { matches(it, track, playCountOf, favourites, nowMs) }
                    if (playlist.matchAll) hits.all { it } else hits.any { it }
                }
            }
        return if (playlist.limit > 0) picked.take(playlist.limit) else picked
    }

    private fun matches(
        rule: SmartRule,
        track: DeviceTrack,
        playCountOf: (String) -> Int,
        favourites: Set<String>,
        nowMs: Long,
    ): Boolean =
        when (rule.field) {
            RuleField.TITLE -> text(rule, track.title)
            RuleField.ARTIST -> text(rule, track.artist)
            RuleField.ALBUM -> text(rule, track.album)
            RuleField.FOLDER -> text(rule, track.folder)
            RuleField.DURATION_MINUTES -> number(rule, track.durationMs / 60_000f)
            RuleField.ADDED_DAYS_AGO -> number(rule, (nowMs / 1000L - track.addedSec) / 86_400f)
            RuleField.PLAY_COUNT -> number(rule, playCountOf(track.uri).toFloat())
            RuleField.FAVOURITE -> (track.uri in favourites) == (rule.op != RuleOp.IS_NOT)
        }

    private fun text(
        rule: SmartRule,
        field: String,
    ): Boolean =
        when (rule.op) {
            RuleOp.CONTAINS -> field.contains(rule.value.trim(), ignoreCase = true)
            RuleOp.IS -> field.equals(rule.value.trim(), ignoreCase = true)
            RuleOp.IS_NOT -> !field.equals(rule.value.trim(), ignoreCase = true)
            RuleOp.AT_LEAST, RuleOp.AT_MOST -> false
        }

    private fun number(
        rule: SmartRule,
        actual: Float,
    ): Boolean {
        val wanted = rule.value.trim().toFloatOrNull() ?: return false
        return when (rule.op) {
            RuleOp.IS -> actual.toInt() == wanted.toInt()
            RuleOp.IS_NOT -> actual.toInt() != wanted.toInt()
            RuleOp.AT_LEAST -> actual >= wanted
            RuleOp.AT_MOST -> actual <= wanted
            RuleOp.CONTAINS -> false
        }
    }
}

/** One JSON file per smart playlist under `files/smart-playlists`, like the ordinary playlists. */
class SmartPlaylistStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "smart-playlists").apply { mkdirs() }

    fun list(): List<SmartPlaylist> =
        dir
            .listFiles { f -> f.extension == "json" }
            ?.mapNotNull { runCatching { fromJson(it.readText()) }.getOrNull() }
            ?.sortedBy { it.name.lowercase() }
            .orEmpty()

    fun save(playlist: SmartPlaylist): Boolean = playlist.name.isNotBlank() && AtomicWrite.text(fileOf(playlist.name), toJson(playlist))

    fun delete(name: String) {
        fileOf(name).delete()
    }

    private fun fileOf(name: String): File = File(dir, PresetStore.safeFileName(name) + ".json")

    private fun toJson(p: SmartPlaylist): String =
        JSONObject()
            .put("name", p.name)
            .put("matchAll", p.matchAll)
            .put("limit", p.limit)
            .put(
                "rules",
                JSONArray(p.rules.map { JSONObject().put("field", it.field.name).put("op", it.op.name).put("value", it.value) }),
            ).toString()

    private fun fromJson(text: String): SmartPlaylist {
        val o = JSONObject(text)
        val rules = o.optJSONArray("rules") ?: JSONArray()
        return SmartPlaylist(
            name = o.getString("name"),
            matchAll = o.optBoolean("matchAll", true),
            limit = o.optInt("limit", 0),
            rules =
                List(rules.length()) { i ->
                    val r = rules.getJSONObject(i)
                    SmartRule(RuleField.valueOf(r.getString("field")), RuleOp.valueOf(r.getString("op")), r.optString("value", ""))
                },
        )
    }
}
