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

    companion object {
        /** About two rows of the grid on a phone: more than that is not "recent" any more. */
        const val RECENT_LIMIT = 12
    }
}

/** One style as the grid's filters see it: what it is called, what kind it is, its family if any. */
data class StyleEntry(
    val id: String,
    val label: String,
    val kindLabel: String,
    val familyLabel: String?,
)

/**
 * The Styles grid's filter chips. One row of chips replaced nine sub-tabs: the user's own
 * shelf first, then each kind of style, then each family.
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
}

object StyleFiltering {
    /**
     * The styles [filter] and [query] leave, in grid order. Recent keeps the shelf's order (newest
     * first) rather than the catalogue's, since that order is the point of the filter. A search
     * matches the name, the kind and the family, every word somewhere.
     */
    fun apply(
        entries: List<StyleEntry>,
        filter: StyleFilter,
        shelf: StyleShelf,
        query: String,
    ): List<StyleEntry> {
        val byFilter =
            when (filter) {
                StyleFilter.All -> entries
                StyleFilter.Favourites -> entries.filter { shelf.isFavourite(it.id) }
                StyleFilter.Recent -> {
                    val byId = entries.associateBy { it.id }
                    shelf.recents.mapNotNull { byId[it] }
                }
                is StyleFilter.Kind -> entries.filter { it.kindLabel == filter.label }
                is StyleFilter.Family -> entries.filter { it.familyLabel == filter.label }
            }
        val terms =
            query
                .trim()
                .split(Regex("\\s+"))
                .filter { it.isNotBlank() }
        if (terms.isEmpty()) return byFilter
        return byFilter.filter { e ->
            val fields = listOfNotNull(e.label, e.kindLabel, e.familyLabel)
            terms.all { t -> fields.any { it.contains(t, ignoreCase = true) } }
        }
    }
}
