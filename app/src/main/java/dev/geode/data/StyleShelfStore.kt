package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.SettingsRepository
import dev.geode.data.prefs.prefKeys
import dev.geode.model.StyleShelf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Keeps the Styles grid's favourites and recents in the `geode-viz` prefs file, so a manual
 * backup carries them with the rest of the visual settings.
 *
 * One instance per process, built by `GeodeContainer` over a [SettingsRepository] ([StyleShelfCodec],
 * #1009, PR 6): the grid is the only writer, and each change applies to the latest shelf.
 */
class StyleShelfStore internal constructor(
    private val repository: SettingsRepository<StyleShelf>,
    private val scope: CoroutineScope,
) {
    val shelf: StateFlow<StyleShelf> = repository.value

    fun toggleFavourite(id: String) = edit { it.toggleFavourite(id) }

    fun pushRecent(id: String) = edit { it.pushRecent(id) }

    fun removeRecent(id: String) = edit { it.removeRecent(id) }

    private fun edit(change: (StyleShelf) -> StyleShelf) {
        scope.launch { repository.update(change) }
    }
}

/** How [StyleShelf] sits in `geode-viz`. */
object StyleShelfCodec : PrefsCodec<StyleShelf> {
    private const val KEY_FAVOURITES = "style_favourites"
    private const val KEY_RECENTS = "style_recents"

    // Style ids are lowercase words and underscores, never a comma.
    private const val SEPARATOR = ","

    override val defaults = StyleShelf()

    override val keys =
        prefKeys(GeodePrefsFiles.VIZ) {
            portable(PrefType.STRING_SET, KEY_FAVOURITES)
            portable(PrefType.STRING, KEY_RECENTS)
        }

    override fun read(prefs: SharedPreferences) =
        StyleShelf(
            favourites = prefs.getStringSet(KEY_FAVOURITES, emptySet()).orEmpty().toSet(),
            recents =
                prefs
                    .getString(KEY_RECENTS, "")
                    .orEmpty()
                    .split(SEPARATOR)
                    .filter { it.isNotBlank() },
        )

    override fun write(
        editor: SharedPreferences.Editor,
        value: StyleShelf,
    ) {
        editor.putStringSet(KEY_FAVOURITES, value.favourites).putString(KEY_RECENTS, value.recents.joinToString(SEPARATOR))
    }
}
