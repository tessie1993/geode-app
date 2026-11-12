package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StyleShelfTest {
    private val order = listOf("arbor_a", "arbor_b", "silk_a", "fluid_a")

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
    fun randomFamilyOnlyIsOffUntilSetAndLeavesTheShelfAlone() {
        val shelf = StyleShelf(favourites = setOf("x"), recents = listOf("y"))
        assertFalse(shelf.randomFamilyOnly)
        val on = shelf.withRandomFamilyOnly(true)
        assertTrue(on.randomFamilyOnly)
        assertEquals(shelf.favourites, on.favourites)
        assertEquals(shelf.recents, on.recents)
        assertFalse(on.withRandomFamilyOnly(false).randomFamilyOnly)
    }

    @Test
    fun yoursIsOnScreenThenFavouritesThenRecentEachOnce() {
        val shelf = StyleShelf(favourites = setOf("silk_a", "arbor_a"), recents = listOf("fluid_a", "missing", "arbor_a"))
        assertEquals(listOf("arbor_b", "arbor_a", "silk_a", "fluid_a"), shelf.yours(order, onScreen = "arbor_b"))
        assertEquals(listOf("fluid_a"), StyleShelf(recents = listOf("fluid_a", "gone")).yours(order, onScreen = null))
        assertEquals(emptyList<String>(), StyleShelf().yours(order, onScreen = "gone"))
    }
}
