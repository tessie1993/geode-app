package dev.geode.data

import android.content.SharedPreferences

class BootAnimationStore(
    private val prefs: SharedPreferences,
) {
    fun load(): Boolean = prefs.getBoolean(KEY_BOOT_ANIM, true)

    fun save(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_BOOT_ANIM, enabled).apply()
    }

    private companion object {
        const val KEY_BOOT_ANIM = "boot_anim"
    }
}
