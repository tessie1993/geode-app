package dev.geode.data

import android.content.SharedPreferences
import androidx.core.content.edit

/**
 * Studio's own small memory, kept across restarts.
 *
 * [lastTab] is the Studio tab the user was last on, so Studio opens where they left it (Render or
 * My videos; an "EDIT" saved before Edit was removed reads as Render, in `StudioSection.fromName`).
 * It is stored as the tab's name, never its position, so reordering the tabs
 * cannot restore onto a different one; an unknown or missing name means "first visit" and the
 * caller falls back to Render. It is a plain string because `data` may not name the UI's own
 * `StudioSection` type.
 *
 * [keepAsProject] answers Render's "add it to My videos?" once and for all: null until the
 * user ticks "don't ask again", then their answer. Settings › Export changes it, null asking again.
 */
class StudioPrefsStore(
    private val prefs: SharedPreferences,
) {
    fun lastTab(): String? = prefs.getString(KEY_TAB, null)

    fun saveLastTab(tab: String) = prefs.edit { putString(KEY_TAB, tab) }

    fun keepAsProject(): Boolean? = if (prefs.contains(KEY_KEEP)) prefs.getBoolean(KEY_KEEP, false) else null

    fun saveKeepAsProject(keep: Boolean?) = prefs.edit { if (keep == null) remove(KEY_KEEP) else putBoolean(KEY_KEEP, keep) }

    private companion object {
        const val KEY_TAB = "studio_last_tab"
        const val KEY_KEEP = "studio_keep_as_project"
    }
}
