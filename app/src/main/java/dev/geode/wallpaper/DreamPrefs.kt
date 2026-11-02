package dev.geode.wallpaper

import android.content.Context
import android.content.SharedPreferences

/**
 * The dream's own options: what [DreamOverlay] draws over the visual.
 *
 * Both on by default (owner, wave 1: "optional clock + now-playing overlay"): a screen saver
 * on a charger is mostly looked at to see the time. Their own file, like the wallpaper's
 * ([WallpaperPrefsStore]), so they never share a key space with the app's stores. The Settings
 * row that flips them belongs to the Settings screens; until it exists these are the defaults.
 * The run rule's options (rest when silent, battery saver) are not here: the dream keeps the
 * wallpaper's battery-friendly defaults, see [VisualizerDreamService].
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
