package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** [BookmarkStore]: at most its cap of tracks keep a bookmark, and past it the one saved longest ago goes. */
class BookmarkStoreTest {
    private fun bookmarks(prefs: MemoryPrefs) =
        prefs.values.keys
            .filter { it.startsWith("bookmark:") }
            .toSet()

    @Test
    fun theBookmarkSavedLongestAgoGoesPastTheCap() {
        val prefs = MemoryPrefs()
        val store = BookmarkStore(prefs, cap = 3)
        for (track in listOf("a", "b", "c")) store.save("content://$track", 60_000L)
        store.save("content://a", 90_000L)
        store.save("content://d", 60_000L)
        assertEquals(setOf("bookmark:content://c", "bookmark:content://a", "bookmark:content://d"), bookmarks(prefs))
        assertNull(store.positionFor("content://b"))
        assertEquals(90_000L, store.positionFor("content://a"))
    }

    @Test
    fun bookmarksFromBeforeTheOrderWasKeptAreTheOldest() {
        val prefs = MemoryPrefs()
        prefs.values["bookmark:content://old1"] = 60_000L
        prefs.values["bookmark:content://old2"] = 60_000L
        val store = BookmarkStore(prefs, cap = 2)
        store.save("content://new", 60_000L)
        assertEquals(2, bookmarks(prefs).size)
        assertEquals(60_000L, store.positionFor("content://new"))
    }

    @Test
    fun aClearedBookmarkLeavesTheOrder() {
        val prefs = MemoryPrefs()
        val store = BookmarkStore(prefs, cap = 2)
        store.save("content://a", 60_000L)
        store.save("content://b", 60_000L)
        store.clear("content://a")
        store.save("content://c", 60_000L)
        assertEquals(setOf("bookmark:content://b", "bookmark:content://c"), bookmarks(prefs))
    }

    @Test
    fun theSameTrackSavedAgainKeepsItsOneBookmark() {
        val prefs = MemoryPrefs()
        val store = BookmarkStore(prefs, cap = 2)
        repeat(5) { store.save("content://a", 60_000L + it) }
        assertEquals(setOf("bookmark:content://a"), bookmarks(prefs))
        assertEquals(60_004L, store.positionFor("content://a"))
    }
}
