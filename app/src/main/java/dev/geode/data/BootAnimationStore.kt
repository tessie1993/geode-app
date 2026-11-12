package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys

class BootAnimationStore(
    private val prefs: SharedPreferences,
) {
    fun load(): Boolean = prefs.getBoolean(KEY_BOOT_ANIM, true)

    fun save(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_BOOT_ANIM, enabled).apply()
    }

    internal companion object {
        const val KEY_BOOT_ANIM = "boot_anim"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                portable(PrefType.BOOLEAN, KEY_BOOT_ANIM)
            }
    }
}
