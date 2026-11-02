package dev.geode.data

import android.content.SharedPreferences
import androidx.core.content.edit
import dev.geode.data.backup.BackupFormat

/**
 * Settings › Backup & about › Reset all settings: every option goes back to what a fresh install
 * starts with. The owner chose "settings only". The user's music, folders, playlists, favourites,
 * presets, macros, modulation, projects, the look on screen, Pro and the tour's progress all stay.
 *
 * It works by removing keys, so each store reads its own default on the next load, and the defaults
 * keep a single home in their stores. What is removed:
 * - `geode-prefs`: everything except the creative keys the macros and modulation borrow there
 *   ([MacroStore.KEYS], [LfoStore.KEYS]) and the install's "seen it" flags and picked URIs
 *   ([BackupFormat.deviceLocalKeys]). Performance is removed too, so it is picked again for this
 *   phone, as on a fresh install.
 * - `geode-player`: everything except each long track's saved position (`bookmark:` keys).
 * - `geode-audiofx`: all of it (equalizer, bass, loudness).
 * - `geode-viz`: only Auto's timer options. The look on screen, the style shelf and the visual
 *   playlist's entries are the user's content.
 *
 * It writes with commit, so call it off the main thread. The stores in a running app hold what they
 * loaded, so the caller restarts the app after this (Settings does, with the launch intent).
 */
class SettingsReset(
    private val files: GeodePrefsFiles,
) {
    fun resetAll() {
        val keepInGeneral =
            BackupFormat.deviceLocalKeys[GeodePrefsFiles.GENERAL].orEmpty() - PerformanceStore.KEYS +
                MacroStore.KEYS + LfoStore.KEYS
        files.general.removeAllBut { it in keepInGeneral }
        files.player.removeAllBut { it.startsWith(BOOKMARK_PREFIX) }
        files.audioFx.removeAllBut { false }
        files.viz.edit { AUTO_TIMER_KEYS.forEach(::remove) }
    }

    private fun SharedPreferences.removeAllBut(keep: (String) -> Boolean) {
        val doomed = all.keys.filterNot(keep)
        edit(commit = true) { doomed.forEach(::remove) }
    }

    private companion object {
        /** `PlayerPrefsStore`'s per-track positions. */
        const val BOOKMARK_PREFIX = "bookmark:"

        /** `ui/AutoVisualsPrefsStore`'s timer options, by name: data may not name the ui package. */
        val AUTO_TIMER_KEYS =
            listOf(
                "auto_random_interval_sec",
                "auto_random_include_styles",
                "auto_random_include_presets",
                "auto_random_include_milk",
                "auto_randomize_colors",
                "auto_playlist_enabled",
                "auto_playlist_interval_sec",
            )
    }
}
