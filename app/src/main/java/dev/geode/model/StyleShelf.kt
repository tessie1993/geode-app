package dev.geode.model

/**
 * The user's own shelf on the Styles page: the styles they starred and the ones they chose last.
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

    /** Your styles: the one [onScreen], then favourites in [order], then recent, each of [order]'s ids once. */
    fun yours(
        order: List<String>,
        onScreen: String?,
    ): List<String> = (listOfNotNull(onScreen) + order.filter(::isFavourite) + recents).distinct().filter { it in order }

    companion object {
        /** About two rows of the grid on a phone: more than that is not "recent" any more. */
        const val RECENT_LIMIT = 12
    }
}

/** One style as the Styles list shows it. */
data class StyleEntry(
    val id: String,
    val label: String,
)
