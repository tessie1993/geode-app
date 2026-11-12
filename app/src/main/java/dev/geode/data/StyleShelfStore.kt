package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.model.StyleShelf
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/**
 * Keeps the Styles page's favourites, recents and Random's "This family only" in the `geode-viz`
 * prefs file, so a manual backup carries them with the rest of the visual settings.
 *
 * Process-wide like [MacroStore]: the grid is the only writer, and a second instance would
 * hold a stale copy of the shelf.
 */
class StyleShelfStore private constructor(
    private val prefs: SharedPreferences,
) {
    private val _shelf = MutableStateFlow(read())
    val shelf: StateFlow<StyleShelf> = _shelf

    fun toggleFavourite(id: String) = edit { it.toggleFavourite(id) }

    fun pushRecent(id: String) = edit { it.pushRecent(id) }

    fun removeRecent(id: String) = edit { it.removeRecent(id) }

    fun setRandomFamilyOnly(on: Boolean) = edit { it.withRandomFamilyOnly(on) }

    private fun edit(change: (StyleShelf) -> StyleShelf) {
        _shelf.update(change)
        val s = _shelf.value
        prefs
            .edit()
            .putStringSet(KEY_FAVOURITES, s.favourites)
            .putString(KEY_RECENTS, s.recents.joinToString(SEPARATOR))
            .putBoolean(KEY_RANDOM_FAMILY_ONLY, s.randomFamilyOnly)
            .apply()
    }

    private fun read(): StyleShelf =
        StyleShelf(
            favourites = prefs.getStringSet(KEY_FAVOURITES, emptySet()).orEmpty().toSet(),
            recents =
                prefs
                    .getString(KEY_RECENTS, "")
                    .orEmpty()
                    .split(SEPARATOR)
                    .filter { it.isNotBlank() },
            randomFamilyOnly = prefs.getBoolean(KEY_RANDOM_FAMILY_ONLY, false),
        )

    companion object {
        private const val KEY_FAVOURITES = "style_favourites"
        private const val KEY_RECENTS = "style_recents"
        private const val KEY_RANDOM_FAMILY_ONLY = "style_random_family_only"

        // Style ids are lowercase words and underscores, never a comma.
        private const val SEPARATOR = ","

        internal val PREF_KEYS =
            prefKeys(GeodePrefsFiles.VIZ) {
                portable(PrefType.STRING_SET, KEY_FAVOURITES)
                portable(PrefType.STRING, KEY_RECENTS)
                portable(PrefType.BOOLEAN, KEY_RANDOM_FAMILY_ONLY)
            }

        @Volatile private var instance: StyleShelfStore? = null

        fun get(context: Context): StyleShelfStore =
            instance ?: synchronized(this) {
                instance ?: StyleShelfStore(GeodePrefsFiles(context.applicationContext).viz).also { instance = it }
            }
    }
}
