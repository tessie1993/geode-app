package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/**
 * Where the user left Visuals › Customize: the tab on screen and the groups they opened.
 *
 * The owner's rule (2026-11-12): every group starts collapsed and one the user opens stays open,
 * across leaving the page and across restarts, so this is kept in prefs, not in saved instance
 * state. A group is named by its path, `CATEGORY/section` (the native map's ids, which outlive a
 * reordered panel); a path the map no longer has is simply never drawn. Nothing here decides what a
 * group holds: that is core/viz/CustomizeMap.
 *
 * Process-wide like [StyleShelfStore]: the panel is the only writer, and a second instance would hold
 * a stale set.
 */
class CustomizeGroupsStore internal constructor(
    private val prefs: SharedPreferences,
) {
    private val _open = MutableStateFlow(prefs.getStringSet(KEY_OPEN, emptySet()).orEmpty().toSet())

    /** The open groups, as `CATEGORY/section` paths. */
    val open: StateFlow<Set<String>> = _open

    private val _tab = MutableStateFlow(prefs.getString(KEY_TAB, null))

    /** The category tab last on screen, by name; null before the first visit. */
    val tab: StateFlow<String?> = _tab

    fun toggle(path: String) = editOpen { if (path in it) it - path else it + path }

    fun expand(path: String) = editOpen { it + path }

    /**
     * Closes every open group of [category]; null closes them in every tab. Collapse all acts on
     * the tab on screen today; passing null at its call site makes it act on all of them.
     */
    fun collapseAll(category: String?) =
        editOpen { open ->
            open.filterTo(mutableSetOf()) { category != null && !it.startsWith("$category/") }
        }

    fun selectTab(name: String) {
        _tab.value = name
        prefs.edit { putString(KEY_TAB, name) }
    }

    private fun editOpen(change: (Set<String>) -> Set<String>) {
        _open.update(change)
        prefs.edit { putStringSet(KEY_OPEN, _open.value) }
    }

    companion object {
        private const val KEY_OPEN = "customize_open_groups"
        private const val KEY_TAB = "customize_tab"

        /** Remembered by this install, never restored onto another phone: where a screen was left, like the tour's progress. */
        val KEYS: Set<String> = setOf(KEY_OPEN, KEY_TAB)

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.GENERAL) {
                state(PrefType.STRING_SET, KEY_OPEN)
                state(PrefType.STRING, KEY_TAB)
            }

        @Volatile private var instance: CustomizeGroupsStore? = null

        fun get(context: Context): CustomizeGroupsStore =
            instance ?: synchronized(this) {
                instance ?: CustomizeGroupsStore(GeodePrefsFiles(context.applicationContext).general).also { instance = it }
            }
    }
}
