package dev.geode.wallpaper

import android.content.Context
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.Settings

/**
 * The system's Remove animations (Settings › Accessibility, or the animator duration scale in
 * Developer options), followed live: one of the two reduced-motion sources the wallpaper and the
 * dream hand the native run rule (ADR 0008; the other is the in-app Reduce motion switch).
 *
 * The setting itself, `ANIMATOR_DURATION_SCALE == 0`, is read rather than
 * `ValueAnimator.areAnimatorsEnabled()`, because that one is also false in battery saver, which has
 * its own option ("pause in battery saver") the user may have turned off; reading it would still a
 * wallpaper the user asked to keep moving. Neither surface has an Activity that a configuration
 * change would restart, so the change comes through a [ContentObserver] on the setting's URI and
 * [onChanged] restates the run mode, the same as a battery-saver broadcast does.
 *
 * Main thread: the observer is delivered on the main looper, where both services restate.
 */
class SystemAnimations(
    context: Context,
    private val onChanged: () -> Unit,
) {
    private val resolver = context.applicationContext.contentResolver

    private val observer =
        object : ContentObserver(Handler(Looper.getMainLooper())) {
            override fun onChange(selfChange: Boolean) = onChanged()
        }

    private var registered = false

    /** Remove animations is on now. A missing value is the system default, 1 (animations on). */
    val removed: Boolean
        get() = Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f

    /** Idempotent, like [stop]: lifecycle callbacks can overlap. */
    fun start() {
        if (registered) return
        resolver.registerContentObserver(Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false, observer)
        registered = true
    }

    fun stop() {
        if (!registered) return
        resolver.unregisterContentObserver(observer)
        registered = false
    }
}
