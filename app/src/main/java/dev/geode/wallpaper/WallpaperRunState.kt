package dev.geode.wallpaper

/** What the wallpaper's GL view does right now. */
enum class WallpaperRunMode {
    /** Paused: nothing on screen to draw to, or nobody can see it. */
    STOPPED,

    /** One frame drawn, then no pacing: a picture that costs nothing while it sits there. */
    STILL,

    /** Paced frames, the live visual. */
    ANIMATED,
}

/**
 * Everything that decides [WallpaperRunMode], gathered so the rule is one pure function rather
 * than branches spread over the engine's callbacks.
 */
data class WallpaperConditions(
    val surfaceAvailable: Boolean,
    val visible: Boolean,
    /** Pro or the trial: free users get a still frame of the style. */
    val animationAllowed: Boolean,
    /** The system wallpaper picker's preview, which should always look alive. */
    val preview: Boolean,
    /** The shade or recents is over the wallpaper (`Engine.onZoomChanged` near 1). */
    val covered: Boolean,
    val batterySaver: Boolean,
    val silent: Boolean,
    val settings: WallpaperSettings,
)

/**
 * The run rule, in priority order.
 *
 * - Not visible (another app in front, screen off) or no surface: stopped, as before.
 * - Covered by the shade or recents: stopped too. Visibility stays true there, so without this
 *   the wallpaper rendered at full rate behind a blur nobody reads.
 * - No Pro: a still frame.
 * - Battery saver (when the user keeps that option on) and silence (the ring [SoundSource]
 *   hears resting: the one native answer and hold every surface reads, #491): a still frame. The preview is exempt from both, so picking the wallpaper
 *   in a quiet room never shows what looks like a frozen picture.
 */
fun wallpaperRunMode(c: WallpaperConditions): WallpaperRunMode =
    when {
        !c.surfaceAvailable || !c.visible -> WallpaperRunMode.STOPPED
        c.covered && !c.preview -> WallpaperRunMode.STOPPED
        !c.animationAllowed -> WallpaperRunMode.STILL
        c.preview -> WallpaperRunMode.ANIMATED
        c.batterySaver && c.settings.pauseInBatterySaver -> WallpaperRunMode.STILL
        c.silent && c.settings.restWhenSilent -> WallpaperRunMode.STILL
        else -> WallpaperRunMode.ANIMATED
    }
