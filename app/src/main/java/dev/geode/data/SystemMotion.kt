package dev.geode.data

import android.content.ContentResolver
import android.provider.Settings

/**
 * Reduced motion, as the one flag the whole app reads: Android's own (#336, story 47).
 *
 * WHY only the system's: the app used to have its own "Slow the motion down" switch beside
 * Android's "Remove animations", so a user who wanted less motion had to say it twice, and the
 * renderer, the paywall and exports only heard the app's switch. Android has no separate
 * "reduce motion" setting; "Remove animations" (Settings › Accessibility › Color and motion) and
 * the developer option both set the animator duration scale to 0, the signal the platform's own
 * views follow. Everything here reads that one number:
 *
 * - the UI motion scheme, through `ui/theme/rememberSystemReducedMotion` (watched live);
 * - the live renderer (`ui/EnginePlumbing`), from the same live value, so turning it on while the
 *   visual plays calms the visual at once;
 * - the paywall's hero, through the same value in `ui/AppShell`;
 * - exports, casts and the wallpaper, read with [isReduced] when they start.
 *
 * Kept in `data` (it reads a stored system setting) so the wallpaper and cast, which may not name
 * `dev.geode.ui`, read the same rule.
 */
object SystemMotion {
    /** The scale Android reports when nothing has set one: animations at normal speed. */
    const val DEFAULT_SCALE = 1f

    /** Whether motion is reduced at this animator duration scale: only an exact 0 turns it on. */
    fun reducedAt(animatorScale: Float): Boolean = animatorScale == 0f

    /** The system's animator duration scale now. */
    fun animatorScale(resolver: ContentResolver): Float =
        Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, DEFAULT_SCALE)

    /** Whether the system asks for reduced motion now. */
    fun isReduced(resolver: ContentResolver): Boolean = reducedAt(animatorScale(resolver))
}
