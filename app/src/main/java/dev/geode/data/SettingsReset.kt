package dev.geode.data

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import dagger.hilt.android.qualifiers.ApplicationContext
import dev.geode.RingLog
import dev.geode.data.backup.BackupFormat
import dev.geode.data.backup.BackupSection
import dev.geode.di.IoDispatcher
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Settings › Backup & about › "Reset all settings": every setting back to what a fresh install
 * starts with, and nothing the user made or chose to keep.
 *
 * What a setting is comes from the backup's own map, so the two never disagree: a key is reset
 * when [BackupFormat.sectionOfKey] files it under [BackupSection.SETTINGS]. That leaves alone,
 * without a list of its own:
 * - Creative's keys borrowed from `geode-prefs` (macros, LFOs, envelopes, reaction routes), the
 *   modulation file, favourites, playlists, presets, projects, takes and Milk imports: the user's
 *   work, which a reset must never cost.
 * - Pro and the trial (`geode-pro`), which are purchases, not settings.
 * - The widget's now-playing state (`geode-widget`), which is a cache.
 *
 * Of the keys the backup calls device-local, three kinds are kept and two are reset:
 * - Kept: the "seen it" flags (the safety notice, first run and the tour are not shown again),
 *   the music folders and the preset mirror folder (their read grants belong to this install,
 *   and dropping them would empty the library rather than reset it).
 * - Reset: Settings › Performance ([PerformanceStore.KEYS]), which [PerformanceStore] re-picks for
 *   this phone on the next start exactly as it does on a fresh install.
 *
 * The look on screen stays too (owner: "settings only"). `geode-viz` is the live look, the style
 * shelf's favourites and recents and the visual playlist, so only Auto's timer options are reset
 * there ([AUTO_TIMER_KEYS]); `geode-background` (the background image and its blend) is kept whole.
 *
 * The live wallpaper's options (`geode-wallpaper`) are settings in a file of their own and are
 * reset whole.
 *
 * Stores and controllers read their files once and keep them in memory, and several push their
 * values to native at app start, so like a backup restore a reset is only whole after the app
 * restarts: the caller asks the user to restart once [resetAll] returns `true`.
 */
@Singleton
class SettingsReset
    @Inject
    constructor(
        @ApplicationContext private val context: Context,
        @IoDispatcher private val io: CoroutineDispatcher,
    ) {
        /**
         * Removes every setting key, so each store reads its fresh-install default. Returns
         * `false` if a file could not be written; the files that were written stay reset, which
         * is no worse than a partial reset by hand, and running it again finishes the job.
         */
        suspend fun resetAll(): Boolean =
            withContext(io) {
                val prefsFiles = BackupSection.SETTINGS.prefsFiles + WALLPAPER_PREFS
                val failed = prefsFiles.filterNot { name -> reset(name) }
                if (failed.isEmpty()) RingLog.note(TAG, "settings reset") else RingLog.note(TAG, "reset failed for $failed")
                failed.isEmpty()
            }

        // commit(), not apply(): the caller restarts the process right after, which would lose
        // an apply() still queued.
        @SuppressLint("ApplySharedPref")
        private fun reset(name: String): Boolean {
            val prefs = prefsFile(name)
            val keys = prefs.all.keys.filter { key -> isSetting(name, key) }
            if (keys.isEmpty()) return true
            val editor = prefs.edit()
            keys.forEach(editor::remove)
            return editor.commit()
        }

        private fun isSetting(
            prefsFile: String,
            key: String,
        ): Boolean =
            when (prefsFile) {
                WALLPAPER_PREFS -> true
                GeodePrefsFiles.VIZ -> key in AUTO_TIMER_KEYS
                GeodePrefsFiles.BACKGROUND -> false
                else ->
                    BackupFormat.sectionOfKey(prefsFile, key) == BackupSection.SETTINGS ||
                        key in resetDeviceLocalKeys[prefsFile].orEmpty()
            }

        // The framework caches one instance per name for the process, so this is the same
        // object GeodePrefsFiles and every store already hold.
        private fun prefsFile(name: String): SharedPreferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)

        private companion object {
            const val TAG = "SettingsReset"

            /** `WallpaperPrefsStore`'s file; its name is private there. */
            const val WALLPAPER_PREFS = "geode-wallpaper"

            /** Device-local keys that are still settings, so a reset clears them (see the class KDoc). */
            val resetDeviceLocalKeys: Map<String, Set<String>> = mapOf(GeodePrefsFiles.GENERAL to PerformanceStore.KEYS)

            /** `ui/AutoVisualsPrefsStore`'s timer options, by name: data may not name the ui package. */
            val AUTO_TIMER_KEYS: Set<String> =
                setOf(
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
