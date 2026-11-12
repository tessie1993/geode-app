package dev.geode.wallpaper

import android.content.SharedPreferences
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative

/** How the wallpaper draws on one screen. */
enum class WallpaperLook {
    /** The whole visual, edge to edge. */
    FULL,

    /**
     * The visual only along the screen's rounded edges, black in the middle.
     *
     * This is the app's always-on-display look. A third-party wallpaper cannot draw in ambient
     * mode (`Engine.onAmbientModeChanged` and `supportsAmbientMode` are system APIs), so the
     * nearest honest thing is a lock and home look that leaves OLED pixels off where icons and
     * the clock sit and lets the music run round the rim.
     */
    EDGE,
}

/**
 * The wallpaper's own options, set from the system picker's Settings button
 * (`ui/wallpaper/WallpaperOptionsActivity`).
 *
 * Defaults are the battery-friendly ones: a wallpaper the user forgets about must not drain the
 * phone, and every one of these can be switched back on by someone who wants it.
 */
data class WallpaperSettings(
    /** Hold the last frame and stop drawing once the sound has been silent for a few seconds. */
    val restWhenSilent: Boolean = true,
    /** Hold a still frame while battery saver is on. */
    val pauseInBatterySaver: Boolean = true,
    /**
     * Hear whatever any app is playing, through a `Visualizer` on the output mix. Only takes
     * effect once the user has granted the microphone permission that API requires. Off until
     * the user turns it on (owner, 2026-11-02, after the Play policy audit): the permission was
     * granted for Live input, so using it here is the user's choice, and the privacy policy
     * names it.
     */
    val hearOtherApps: Boolean = false,
    val homeLook: WallpaperLook = WallpaperLook.FULL,
    val lockLook: WallpaperLook = WallpaperLook.FULL,
    /**
     * The wallpaper's own frame rate (ADR 0008), apart from Settings › Performance's cap for the live
     * visual: 30 unless the user picks another. Stored as the user's int; the allowed rates and the
     * snap are native (`GeodeNative.surfaceFpsChoice`), so the pacer only ever
     * sees a listed rate, whatever an older or newer build wrote here.
     */
    val fps: Int = GeodeNative.SURFACE_DEFAULT_FPS,
)

/**
 * Keeps [WallpaperSettings] in their own preference file, `GeodePrefsFiles.wallpaper`.
 *
 * Their own file, not `GeodePrefsFiles.general`, so the wallpaper's options never share a key
 * space with the app's stores; Settings › Backup carries the file with Settings (#994).
 * Read on the wallpaper engine's main thread on every visibility change: SharedPreferences
 * caches the file after the first read, so a re-read costs a map lookup.
 */
class WallpaperPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun load(): WallpaperSettings {
        val d = WallpaperSettings()
        return WallpaperSettings(
            restWhenSilent = prefs.getBoolean(KEY_REST, d.restWhenSilent),
            pauseInBatterySaver = prefs.getBoolean(KEY_SAVER, d.pauseInBatterySaver),
            hearOtherApps = prefs.getBoolean(KEY_OTHER_APPS, d.hearOtherApps),
            homeLook = look(KEY_HOME_LOOK, d.homeLook),
            lockLook = look(KEY_LOCK_LOOK, d.lockLook),
            fps = prefs.getInt(KEY_FPS, d.fps),
        )
    }

    fun save(settings: WallpaperSettings) {
        prefs
            .edit()
            .putBoolean(KEY_REST, settings.restWhenSilent)
            .putBoolean(KEY_SAVER, settings.pauseInBatterySaver)
            .putBoolean(KEY_OTHER_APPS, settings.hearOtherApps)
            .putString(KEY_HOME_LOOK, settings.homeLook.name)
            .putString(KEY_LOCK_LOOK, settings.lockLook.name)
            .putInt(KEY_FPS, settings.fps)
            .apply()
    }

    /** The last output-mix capture this phone refused ([OutputMixCapture.start]); cleared when one opens. */
    fun mixRefused(): Boolean = prefs.getBoolean(KEY_MIX_REFUSED, false)

    fun saveMixRefused(refused: Boolean) {
        if (mixRefused() != refused) prefs.edit().putBoolean(KEY_MIX_REFUSED, refused).apply()
    }

    private fun look(
        key: String,
        fallback: WallpaperLook,
    ): WallpaperLook = prefs.getString(key, null)?.let { name -> WallpaperLook.entries.firstOrNull { it.name == name } } ?: fallback

    internal companion object {
        const val FILE = GeodePrefsFiles.WALLPAPER
        const val KEY_REST = "rest_when_silent"
        const val KEY_SAVER = "pause_in_battery_saver"
        const val KEY_OTHER_APPS = "hear_other_apps"
        const val KEY_HOME_LOOK = "home_look"
        const val KEY_LOCK_LOOK = "lock_look"
        const val KEY_FPS = "fps"
        const val KEY_MIX_REFUSED = "mix_refused"

        /** The options ride with Settings in a backup; what this phone's output mix refused stays here. */
        val PREF_KEYS =
            prefKeys(FILE) {
                portable(PrefType.BOOLEAN, KEY_REST, KEY_SAVER, KEY_OTHER_APPS)
                portable(PrefType.STRING, KEY_HOME_LOOK, KEY_LOCK_LOOK)
                portable(PrefType.INT, KEY_FPS)
                device(PrefType.BOOLEAN, KEY_MIX_REFUSED)
            }
    }
}
