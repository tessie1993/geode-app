package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.AutoNative
import dev.geode.engine.bridge.ShellNative
import dev.geode.model.StyleCatalogue
import dev.geode.model.VizPlaylistEntry

/**
 * Auto's remembered shape: the timed style change ("change style every N s", still called Random in
 * the code), what it picks from, and the visual playlist.
 *
 * Auto is one mode now: the Auto Director drives the look (on by default, [dev.geode.data.AutoDirectorStore];
 * its switch is the first row of Visuals › Settings) and the timer is an optional extra on top. The timer
 * itself always starts OFF. It used to be restored from `auto_random_enabled`, and that is how Random came
 * up ON on what looked like a fresh install: the key rides along with Android's own app backup and with
 * Settings › Backup, so a reinstall brought the old phone's running timer back. A timer is something the listener
 * starts on purpose, for this session; what it does once started (interval, sources, colours) is
 * remembered. The old key is removed on the next save.
 *
 * This store only reads and writes the prefs: what the values mean (the playlist's JSON, each scene id
 * resolved, the playlist off without entries, the intervals held to range, the timer off) is native's
 * ([Rules], core/shell/AutoVisuals).
 */
class AutoVisualsPrefsStore(
    private val prefs: SharedPreferences,
    private val rules: Rules = NativeRules,
) {
    /** Auto's saved-shape rules. The app asks native; a JVM test, which cannot load the library, hands in its own. */
    interface Rules {
        fun entriesFromJson(json: String): List<VizPlaylistEntry>

        fun entriesToJson(entries: List<VizPlaylistEntry>): String

        /** The `ShellNative.AUTO_SAVED_*` fields read: [state] now, [stored] where [has], the playlist's [playlistCount]. */
        fun apply(
            state: IntArray,
            stored: IntArray,
            has: BooleanArray,
            playlistCount: Int,
        ): IntArray
    }

    private object NativeRules : Rules {
        override fun entriesFromJson(json: String): List<VizPlaylistEntry> =
            ShellNative
                .autoPlaylistRead(json)
                .asList()
                .chunked(ShellNative.AUTO_ENTRY_FIELDS)
                .map { f -> VizPlaylistEntry(sceneId = f[0].orEmpty(), presetName = f[1], label = f[2].orEmpty()) }
                // An entry of a style this build no longer has is dropped, never played.
                .filter { it.sceneId in StyleCatalogue.ids }

        override fun entriesToJson(entries: List<VizPlaylistEntry>): String =
            ShellNative.autoPlaylistToJson(
                entries.flatMap { listOf(it.sceneId, it.presetName, it.label) }.toTypedArray(),
            )

        override fun apply(
            state: IntArray,
            stored: IntArray,
            has: BooleanArray,
            playlistCount: Int,
        ): IntArray = ShellNative.autoVisualsApply(state, stored, has, playlistCount)
    }

    fun applyTo(state: VizUiState): VizUiState {
        val entries = prefs.getString(KEY_PLAYLIST_ENTRIES, null)?.let(rules::entriesFromJson) ?: state.vizPlaylist
        val now =
            intArrayOf(
                bit(state.randomEnabled),
                state.randomIntervalSec,
                bit(state.randomIncludeStyles),
                bit(state.randomIncludePresets),
                bit(state.vizPlaylistEnabled),
                state.vizPlaylistIntervalSec,
            )
        val has = BooleanArray(ShellNative.AUTO_SAVED_FIELDS) { k -> SAVED_KEYS[k]?.let(prefs::contains) == true }
        val stored =
            IntArray(ShellNative.AUTO_SAVED_FIELDS) { k ->
                val key = SAVED_KEYS[k]
                when {
                    key == null || !has[k] -> 0
                    k == ShellNative.AUTO_SAVED_RANDOM_INTERVAL || k == ShellNative.AUTO_SAVED_PLAYLIST_INTERVAL -> prefs.getInt(key, 0)
                    else -> bit(prefs.getBoolean(key, false))
                }
            }
        val read = rules.apply(now, stored, has, entries.size)
        return state.copy(
            randomEnabled = read[ShellNative.AUTO_SAVED_RANDOM_ON] != 0,
            randomIntervalSec = read[ShellNative.AUTO_SAVED_RANDOM_INTERVAL],
            randomIncludeStyles = read[ShellNative.AUTO_SAVED_RANDOM_STYLES] != 0,
            randomIncludePresets = read[ShellNative.AUTO_SAVED_RANDOM_PRESETS] != 0,
            vizPlaylist = entries,
            vizPlaylistEnabled = read[ShellNative.AUTO_SAVED_PLAYLIST_ON] != 0,
            vizPlaylistIntervalSec = read[ShellNative.AUTO_SAVED_PLAYLIST_INTERVAL],
        )
    }

    fun save(state: VizUiState) {
        prefs
            .edit()
            .remove(LEGACY_KEY_RANDOM_ENABLED)
            .putInt(KEY_RANDOM_INTERVAL, state.randomIntervalSec)
            .putBoolean(KEY_RANDOM_STYLES, state.randomIncludeStyles)
            .putBoolean(KEY_RANDOM_PRESETS, state.randomIncludePresets)
            .putBoolean(KEY_PLAYLIST_ENABLED, state.vizPlaylistEnabled)
            .putInt(KEY_PLAYLIST_INTERVAL, state.vizPlaylistIntervalSec)
            .putString(KEY_PLAYLIST_ENTRIES, rules.entriesToJson(state.vizPlaylist))
            .apply()
    }

    companion object {
        /** Auto's timer intervals, native's range (core/viz/AutoPlan). */
        val INTERVAL_SEC: IntRange = AutoNative.AUTO_INTERVAL_MIN_S..AutoNative.AUTO_INTERVAL_MAX_S

        /** Read by no build since the timer stopped being restored; removed on the next save. */
        private const val LEGACY_KEY_RANDOM_ENABLED = "auto_random_enabled"
        private const val KEY_RANDOM_INTERVAL = "auto_random_interval_sec"
        private const val KEY_RANDOM_STYLES = "auto_random_include_styles"
        private const val KEY_RANDOM_PRESETS = "auto_random_include_presets"
        private const val KEY_PLAYLIST_ENABLED = "auto_playlist_enabled"
        private const val KEY_PLAYLIST_INTERVAL = "auto_playlist_interval_sec"
        private const val KEY_PLAYLIST_ENTRIES = "auto_playlist_entries"

        /** The key each `ShellNative.AUTO_SAVED_*` field is kept under; the timer is never kept. */
        private val SAVED_KEYS: List<String?> =
            listOf(
                null,
                KEY_RANDOM_INTERVAL,
                KEY_RANDOM_STYLES,
                KEY_RANDOM_PRESETS,
                KEY_PLAYLIST_ENABLED,
                KEY_PLAYLIST_INTERVAL,
            )

        private fun bit(on: Boolean): Int = if (on) 1 else 0

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.VIZ) {
                portable(PrefType.INT, KEY_RANDOM_INTERVAL, KEY_PLAYLIST_INTERVAL)
                portable(PrefType.BOOLEAN, KEY_RANDOM_STYLES, KEY_RANDOM_PRESETS, KEY_PLAYLIST_ENABLED)
                portable(PrefType.STRING, KEY_PLAYLIST_ENTRIES)
                retired(null, LEGACY_KEY_RANDOM_ENABLED)
            }
    }
}
