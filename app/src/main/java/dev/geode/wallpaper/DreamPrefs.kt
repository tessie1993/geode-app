package dev.geode.wallpaper

import android.content.Context
import android.content.SharedPreferences

/**
 * The dream's own toggles: what [DreamOverlay] draws over the visual.
 *
 * Both on by default (owner, wave 1: "optional clock + now-playing overlay"): a dream on a
 * charger is mostly a bedside clock. Their own file, like the wallpaper's ([WallpaperPrefsStore]),
 * so the dream never writes into another store's keys; its brightness stays in
 * `BehaviourPrefs.dreamScreenBright`, which Settings › Behaviour already owns. The Settings rows
 * that flip these come with the Settings rebuild; until then they are the defaults.
 */
data class DreamSettings(
    val showClock: Boolean = true,
    val showNowPlaying: Boolean = true,
)

class DreamPrefsStore(
    context: Context,
) {
    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    fun load(): DreamSettings {
        val d = DreamSettings()
        return DreamSettings(
            showClock = prefs.getBoolean(KEY_CLOCK, d.showClock),
            showNowPlaying = prefs.getBoolean(KEY_NOW_PLAYING, d.showNowPlaying),
        )
    }

    fun save(settings: DreamSettings) {
        prefs
            .edit()
            .putBoolean(KEY_CLOCK, settings.showClock)
            .putBoolean(KEY_NOW_PLAYING, settings.showNowPlaying)
            .apply()
    }

    private companion object {
        const val FILE = "geode-dream"
        const val KEY_CLOCK = "show_clock"
        const val KEY_NOW_PLAYING = "show_now_playing"
    }
}
