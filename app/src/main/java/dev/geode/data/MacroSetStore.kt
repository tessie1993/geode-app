package dev.geode.data

import android.content.SharedPreferences
import dev.geode.render.scene.MacroSet
import dev.geode.render.scene.MacroSetCodec
import dev.geode.render.scene.StandardMacros

/**
 * The active macro set, kept in prefs (text in [MacroSetCodec]'s format) so the knobs come back
 * where they were. The set is global, not per preset: switching style or loading a preset keeps
 * the same knobs.
 *
 * Small and synchronous on purpose; callers run it on the session's store dispatcher.
 */
class MacroSetStore(
    private val prefs: SharedPreferences,
) {
    fun loadActive(): MacroSet =
        prefs.getString(KEY_ACTIVE, null)?.let(MacroSetCodec::decode)?.takeIf { it.macros.isNotEmpty() }
            ?: StandardMacros.SET

    fun saveActive(set: MacroSet) {
        prefs.edit().putString(KEY_ACTIVE, MacroSetCodec.encode(set)).apply()
    }

    private companion object {
        const val KEY_ACTIVE = "macro_set_active"
    }
}
