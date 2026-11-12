package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Where the user left Visuals › Customize: the tab on screen and the groups they opened.
 *
 * The owner's rule (2026-11-12): every group starts collapsed and one the user opens stays open,
 * across leaving the page and across restarts, so this is kept in prefs, not in saved instance
 * state. A group is named by its path, `CATEGORY/section` (the native map's ids, which outlive a
 * reordered panel); a path the map no longer has is simply never drawn. Nothing here decides what a
 * group holds: that is core/viz/CustomizeMap.
 */
data class CustomizeGroups(
    /** The open groups, as `CATEGORY/section` paths. */
    val open: Set<String> = emptySet(),
    /** The category tab last on screen, by name; null before the first visit. */
    val tab: String? = null,
)

/**
 * One instance per process, built by `GeodeContainer` over a [SettingsRepository]
 * ([CustomizeGroupsCodec]), like [StyleShelfStore]: the panel is the only writer, and each change
 * applies to the latest value.
 */
class CustomizeGroupsStore internal constructor(
    private val repository: SettingsRepository<CustomizeGroups>,
    private val scope: CoroutineScope,
) {
    val groups: StateFlow<CustomizeGroups> = repository.value

    fun toggle(path: String) = edit { it.copy(open = if (path in it.open) it.open - path else it.open + path) }

    fun expand(path: String) = edit { it.copy(open = it.open + path) }

    /**
     * Closes every open group of [category]; null closes them in every tab. Collapse all acts on
     * the tab on screen today; passing null at its call site makes it act on all of them.
     */
    fun collapseAll(category: String?) =
        edit { groups ->
            groups.copy(open = groups.open.filterTo(mutableSetOf()) { category != null && !it.startsWith("$category/") })
        }

    fun selectTab(name: String) = edit { it.copy(tab = name) }

    private fun edit(change: (CustomizeGroups) -> CustomizeGroups) {
        scope.launch { repository.update(change) }
    }
}

/** How [CustomizeGroups] sits in `geode-prefs`: state this install remembers, never in a backup. */
object CustomizeGroupsCodec : PrefsCodec<CustomizeGroups> {
    private const val KEY_OPEN = "customize_open_groups"
    private const val KEY_TAB = "customize_tab"

    /** Screen memory, like the tour's progress: `BackupFormat` leaves these out. */
    val KEYS: Set<String> = setOf(KEY_OPEN, KEY_TAB)

    override val defaults = CustomizeGroups()

    override val keys =
        prefKeys(GeodePrefsFiles.GENERAL) {
            state(PrefType.STRING_SET, KEY_OPEN)
            state(PrefType.STRING, KEY_TAB)
        }

    override fun read(prefs: SharedPreferences) =
        CustomizeGroups(
            open = prefs.getStringSet(KEY_OPEN, emptySet()).orEmpty().toSet(),
            tab = prefs.getString(KEY_TAB, null),
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: CustomizeGroups,
    ) {
        editor.putStringSet(KEY_OPEN, value.open)
        if (value.tab == null) editor.remove(KEY_TAB) else editor.putString(KEY_TAB, value.tab)
    }
}
