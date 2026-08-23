package dev.geode.ui

import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject

class AutoVisualsPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun applyTo(state: VizUiState): VizUiState {
        val entries = prefs.getString(KEY_PLAYLIST_ENTRIES, null)?.let(::entriesFromJson) ?: state.vizPlaylist
        return state.copy(
            randomIntervalSec = prefs.getInt(KEY_RANDOM_INTERVAL, state.randomIntervalSec).coerceIn(INTERVAL_SEC),
            randomOnBeat = prefs.getBoolean(KEY_RANDOM_ON_BEAT, state.randomOnBeat),
            randomIncludeStyles = prefs.getBoolean(KEY_RANDOM_STYLES, state.randomIncludeStyles),
            randomIncludePresets = prefs.getBoolean(KEY_RANDOM_PRESETS, state.randomIncludePresets),
            randomIncludeMilk = prefs.getBoolean(KEY_RANDOM_MILK, state.randomIncludeMilk),
            randomizeColors = prefs.getBoolean(KEY_RANDOM_COLORS, state.randomizeColors),
            vizPlaylist = entries,
            vizPlaylistEnabled = prefs.getBoolean(KEY_PLAYLIST_ENABLED, state.vizPlaylistEnabled) && entries.isNotEmpty(),
            vizPlaylistIntervalSec = prefs.getInt(KEY_PLAYLIST_INTERVAL, state.vizPlaylistIntervalSec).coerceIn(INTERVAL_SEC),
            vizPlaylistIntelligent = prefs.getBoolean(KEY_PLAYLIST_INTELLIGENT, state.vizPlaylistIntelligent),
        )
    }

    fun save(state: VizUiState) {
        prefs
            .edit()
            .putInt(KEY_RANDOM_INTERVAL, state.randomIntervalSec)
            .putBoolean(KEY_RANDOM_ON_BEAT, state.randomOnBeat)
            .putBoolean(KEY_RANDOM_STYLES, state.randomIncludeStyles)
            .putBoolean(KEY_RANDOM_PRESETS, state.randomIncludePresets)
            .putBoolean(KEY_RANDOM_MILK, state.randomIncludeMilk)
            .putBoolean(KEY_RANDOM_COLORS, state.randomizeColors)
            .putBoolean(KEY_PLAYLIST_ENABLED, state.vizPlaylistEnabled)
            .putInt(KEY_PLAYLIST_INTERVAL, state.vizPlaylistIntervalSec)
            .putBoolean(KEY_PLAYLIST_INTELLIGENT, state.vizPlaylistIntelligent)
            .putString(KEY_PLAYLIST_ENTRIES, entriesToJson(state.vizPlaylist))
            .apply()
    }

    companion object {
        val INTERVAL_SEC: IntRange = 5..300

        private const val KEY_RANDOM_INTERVAL = "auto_random_interval_sec"
        private const val KEY_RANDOM_ON_BEAT = "auto_random_on_beat"
        private const val KEY_RANDOM_STYLES = "auto_random_include_styles"
        private const val KEY_RANDOM_PRESETS = "auto_random_include_presets"
        private const val KEY_RANDOM_MILK = "auto_random_include_milk"
        private const val KEY_RANDOM_COLORS = "auto_randomize_colors"
        private const val KEY_PLAYLIST_ENABLED = "auto_playlist_enabled"
        private const val KEY_PLAYLIST_INTERVAL = "auto_playlist_interval_sec"
        private const val KEY_PLAYLIST_INTELLIGENT = "auto_playlist_intelligent"
        private const val KEY_PLAYLIST_ENTRIES = "auto_playlist_entries"

        internal fun entriesToJson(entries: List<VizPlaylistEntry>): String {
            val arr = JSONArray()
            for (e in entries) {
                arr.put(
                    JSONObject()
                        .put("sceneId", e.sceneId)
                        .put("label", e.label)
                        .apply {
                            e.presetName?.let { put("presetName", it) }
                            e.milkPath?.let { put("milkPath", it) }
                        },
                )
            }
            return arr.toString()
        }

        internal fun entriesFromJson(json: String): List<VizPlaylistEntry> =
            runCatching {
                val arr = JSONArray(json)
                buildList {
                    for (i in 0 until arr.length()) {
                        val o = arr.optJSONObject(i) ?: continue
                        val sceneId = o.optString("sceneId", "")
                        if (sceneId.isEmpty()) continue
                        add(
                            VizPlaylistEntry(
                                sceneId = sceneId,
                                presetName = o.optString("presetName", "").takeIf { it.isNotEmpty() },
                                milkPath = o.optString("milkPath", "").takeIf { it.isNotEmpty() },
                                label = o.optString("label", "").ifEmpty { sceneId },
                            ),
                        )
                    }
                }
            }.getOrDefault(emptyList())
    }
}
