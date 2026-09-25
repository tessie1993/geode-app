package dev.geode.data

import android.content.SharedPreferences

/**
 * The one GUI preference read outside the app shell: whether the person asked for reduced
 * motion. The keys are the ones the previous settings screen wrote, so a choice already made
 * survives the shell being rebuilt; the new settings screen should keep writing them.
 */
object MotionPrefs {
    const val KEY_REDUCED_MOTION = "gui_reduced_motion"
    private const val KEY_SAFETY_CHOICE = "gui_safety_choice"
    private const val LEGACY_CHOICE_REDUCED_MOTION = "REDUCED_MOTION"

    /** Reduced motion used to be one of the safety-notice answers; that older form still counts. */
    fun reducedMotion(prefs: SharedPreferences): Boolean =
        prefs.getBoolean(KEY_REDUCED_MOTION, false) ||
            prefs.getString(KEY_SAFETY_CHOICE, null) == LEGACY_CHOICE_REDUCED_MOTION
}
