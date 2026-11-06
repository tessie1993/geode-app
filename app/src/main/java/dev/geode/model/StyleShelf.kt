package dev.geode.model

/**
 * The user's own shelf in the Styles grid: the styles they starred and the ones they chose last.
 *
 * Pure, so the rules (newest first, no repeats, a short memory) are tested off-device; the
 * prefs file that keeps it is `data/StyleShelfStore`.
 */
data class StyleShelf(
    val favourites: Set<String> = emptySet(),
    val recents: List<String> = emptyList(),
) {
    fun isFavourite(id: String): Boolean = id in favourites

    fun toggleFavourite(id: String): StyleShelf = copy(favourites = if (id in favourites) favourites - id else favourites + id)

    /** [id] moves to the front; the list never repeats a style and keeps [RECENT_LIMIT] of them. */
    fun pushRecent(id: String): StyleShelf = copy(recents = (listOf(id) + recents.filterNot { it == id }).take(RECENT_LIMIT))

    /** [id] leaves Recent (the row menu's "Remove from Recent"); favourites are untouched. */
    fun removeRecent(id: String): StyleShelf = copy(recents = recents - id)

    companion object {
        /** About two rows of the grid on a phone: more than that is not "recent" any more. */
        const val RECENT_LIMIT = 12
    }
}

/**
 * One style as the list's filters see it: what it is called, what kind it is, its family if any,
 * and the group it is shown in ([StyleLook]) with that group's name as the user reads it
 * ([lookLabel], a string resource the UI fills in, so a search finds a group in the user's language).
 *
 * A built-in variant (a look of one style: a tuned one such as Fluid's Inkdrop, or a generic
 * one such as Plasma · Chill) is a row of its own:
 * [id] is its preset name, so favourites and Recent keep it apart from its style, [sceneId] is
 * the style it runs on, and [presetName] is the look a tap applies. Styles live in one place, so
 * these rows sit under their style here instead of in the Presets grid.
 */
data class StyleEntry(
    val id: String,
    val label: String,
    val kindLabel: String,
    val familyLabel: String?,
    val look: StyleLook? = null,
    val lookLabel: String? = null,
    val sceneId: String = id,
    val presetName: String? = null,
)

/**
 * What the Styles list shows: everything, the user's own shelf, or one look group ([Look]). Kind
 * and family are no longer offered as filters (a search still finds them by those words).
 */
sealed interface StyleFilter {
    data object All : StyleFilter

    data object Favourites : StyleFilter

    data object Recent : StyleFilter

    data class Kind(
        val label: String,
    ) : StyleFilter

    data class Family(
        val label: String,
    ) : StyleFilter

    data class Look(
        val look: StyleLook,
    ) : StyleFilter
}

object StyleFiltering {
    /**
     * The styles [filter] and [query] leave, in grid order. Recent keeps the shelf's order (newest
     * first) rather than the catalogue's, since that order is the point of the filter.
     *
     * A search looks through every style, not only the chosen group: a standard search box finds
     * what you type wherever it is, and "where did that style go" is what people search to answer.
     * It matches the name, the kind, the family and the group, every word somewhere.
     */
    fun apply(
        entries: List<StyleEntry>,
        filter: StyleFilter,
        shelf: StyleShelf,
        query: String,
    ): List<StyleEntry> {
        val terms =
            query
                .trim()
                .split(Regex("\\s+"))
                .filter { it.isNotBlank() }
        if (terms.isNotEmpty()) {
            return entries.filter { e ->
                val fields = listOfNotNull(e.label, e.kindLabel, e.familyLabel, e.lookLabel)
                terms.all { t -> fields.any { it.contains(t, ignoreCase = true) } }
            }
        }
        return when (filter) {
            StyleFilter.All -> entries
            StyleFilter.Favourites -> entries.filter { shelf.isFavourite(it.id) }
            StyleFilter.Recent -> {
                val byId = entries.associateBy { it.id }
                shelf.recents.mapNotNull { byId[it] }
            }
            is StyleFilter.Kind -> entries.filter { it.kindLabel == filter.label }
            is StyleFilter.Family -> entries.filter { it.familyLabel == filter.label }
            is StyleFilter.Look -> entries.filter { it.look == filter.look }
        }
    }
}
