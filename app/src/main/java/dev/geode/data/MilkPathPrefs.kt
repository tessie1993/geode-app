package dev.geode.data

import android.content.SharedPreferences
import dev.geode.engine.bridge.GeodeNative

/** The shown MilkDrop preset as `GeodePrefsFiles.viz` keeps it; the memory itself is native (core/viz/MilkMemory). */
object MilkPathPrefs {
    /** The key the shown preset has always been saved under, so installs keep it. */
    const val KEY = "milk_path"

    /** Hands native's preset memory the saved path; only a process's first seed counts. */
    fun seed(prefs: SharedPreferences) = GeodeNative.milkMemorySeed(prefs.getString(KEY, null))

    /** Saves the shown preset, or removes it when [path] is null. */
    fun save(
        prefs: SharedPreferences,
        path: String?,
    ) {
        val edit = prefs.edit()
        if (path == null) edit.remove(KEY) else edit.putString(KEY, path)
        edit.apply()
    }
}
