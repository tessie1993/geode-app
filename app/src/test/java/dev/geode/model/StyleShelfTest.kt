package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StyleShelfTest {
    private val entries =
        listOf(
            StyleEntry("arbor_a", "Arbor One", "Shaders", "Arbor"),
            StyleEntry("arbor_b", "Arbor Two", "Shaders", "Arbor"),
            StyleEntry("silk_a", "Silk Veil", "Silk", null),
            StyleEntry("fluid_a", "Ink Bloom", "Fluid", null),
        )

    @Test
    fun recentsAreNewestFirstWithoutRepeats() {
        val shelf =
            StyleShelf()
                .pushRecent("a")
                .pushRecent("b")
                .pushRecent("a")
        assertEquals(listOf("a", "b"), shelf.recents)
    }

    @Test
    fun recentsForgetPastTheLimit() {
        var shelf = StyleShelf()
        repeat(StyleShelf.RECENT_LIMIT + 3) { shelf = shelf.pushRecent("s$it") }
        assertEquals(StyleShelf.RECENT_LIMIT, shelf.recents.size)
        assertEquals("s${StyleShelf.RECENT_LIMIT + 2}", shelf.recents.first())
    }

    @Test
    fun favouriteToggles() {
        val on = StyleShelf().toggleFavourite("x")
        assertTrue(on.isFavourite("x"))
        assertFalse(on.toggleFavourite("x").isFavourite("x"))
    }

    @Test
    fun recentFilterKeepsShelfOrder() {
        val shelf = StyleShelf(recents = listOf("fluid_a", "missing", "arbor_a"))
        val ids = StyleFiltering.apply(entries, StyleFilter.Recent, shelf, "").map { it.id }
        assertEquals(listOf("fluid_a", "arbor_a"), ids)
    }

    @Test
    fun kindFamilyAndFavouriteFilters() {
        val shelf = StyleShelf(favourites = setOf("silk_a"))
        assertEquals(listOf("silk_a"), StyleFiltering.apply(entries, StyleFilter.Favourites, shelf, "").map { it.id })
        assertEquals(2, StyleFiltering.apply(entries, StyleFilter.Family("Arbor"), shelf, "").size)
        assertEquals(listOf("fluid_a"), StyleFiltering.apply(entries, StyleFilter.Kind("Fluid"), shelf, "").map { it.id })
    }

    @Test
    fun searchMatchesEveryWordAcrossNameKindAndFamily() {
        val all = StyleFilter.All
        assertEquals(listOf("arbor_b"), StyleFiltering.apply(entries, all, StyleShelf(), "arbor two").map { it.id })
        assertEquals(listOf("silk_a"), StyleFiltering.apply(entries, all, StyleShelf(), "  silk ").map { it.id })
        assertEquals(entries, StyleFiltering.apply(entries, all, StyleShelf(), " "))
    }
}
