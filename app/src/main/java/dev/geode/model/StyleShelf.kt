package dev.geode.model

import dev.geode.engine.bridge.ShellNative

/**
 * The user's own shelf on the Styles page: the styles they starred, the ones they chose last.
 *
 * The rules (newest first, no repeats, a short memory, the order of "yours") are native's
 * (core/shell/StyleShelf, asked through [Rules]); the prefs file that keeps it is `data/StyleShelfStore`.
 */
data class StyleShelf(
    val favourites: Set<String> = emptySet(),
    val recents: List<String> = emptyList(),
) {
    fun isFavourite(id: String): Boolean = id in favourites

    fun toggleFavourite(id: String): StyleShelf = copy(favourites = Rules.reads.toggleFavourite(favourites.toList(), id).toSet())

    /** [id] moves to the front; the list never repeats a style and keeps native's short memory. */
    fun pushRecent(id: String): StyleShelf = copy(recents = Rules.reads.pushRecent(recents, id))

    /** [id] leaves Recent (the row menu's "Remove from Recent"); favourites are untouched. */
    fun removeRecent(id: String): StyleShelf = copy(recents = Rules.reads.removeRecent(recents, id))

    /** Your styles: the one [onScreen], then favourites in [order], then recent, each of [order]'s ids once. */
    fun yours(
        order: List<String>,
        onScreen: String?,
    ): List<String> = Rules.reads.yours(favourites.toList(), recents, order, onScreen)

    /** The shelf's rules: native's ([ShellNative]). JVM tests cannot load libgeode, so they [install] a stand-in. */
    object Rules {
        interface Reads {
            fun toggleFavourite(
                favourites: List<String>,
                id: String,
            ): List<String>

            fun pushRecent(
                recents: List<String>,
                id: String,
            ): List<String>

            fun removeRecent(
                recents: List<String>,
                id: String,
            ): List<String>

            fun yours(
                favourites: List<String>,
                recents: List<String>,
                order: List<String>,
                onScreen: String?,
            ): List<String>
        }

        private object Native : Reads {
            override fun toggleFavourite(
                favourites: List<String>,
                id: String,
            ): List<String> = ShellNative.styleShelfToggleFavourite(favourites.toTypedArray(), id).asList()

            override fun pushRecent(
                recents: List<String>,
                id: String,
            ): List<String> = ShellNative.styleShelfPushRecent(recents.toTypedArray(), id).asList()

            override fun removeRecent(
                recents: List<String>,
                id: String,
            ): List<String> = ShellNative.styleShelfRemoveRecent(recents.toTypedArray(), id).asList()

            override fun yours(
                favourites: List<String>,
                recents: List<String>,
                order: List<String>,
                onScreen: String?,
            ): List<String> =
                ShellNative.styleShelfYours(favourites.toTypedArray(), recents.toTypedArray(), order.toTypedArray(), onScreen).asList()
        }

        @Volatile
        internal var reads: Reads = Native
            private set

        /** Asks [next] from now on. */
        fun install(next: Reads) {
            reads = next
        }
    }
}

/** One style as the Styles list shows it. */
data class StyleEntry(
    val id: String,
    val label: String,
)
