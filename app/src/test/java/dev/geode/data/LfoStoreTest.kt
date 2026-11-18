package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

/** [LfoStore.loadHandMask]: routes native cannot read are dropped from play but kept whole aside, never destroyed. */
class LfoStoreTest {
    private fun store(
        prefs: MemoryPrefs,
        loaded: Int,
    ) = LfoStore(prefs, readReactions = { null }, handLoad = { _, _, _ -> loaded })

    @Test
    fun unreadRoutesAreKeptAsideBeforeTheyAreDropped() {
        val prefs = MemoryPrefs()
        store(prefs, 0).saveReactions("reactions/9\ntrue|KICK|ZOOM|ADD|0.5|LINEAR|0|0.2", 5)
        assertEquals(0, store(prefs, -1).loadHandMask())
        assertEquals("reactions/9\ntrue|KICK|ZOOM|ADD|0.5|LINEAR|0|0.2", prefs.getString(LfoStore.KEY_REACTIONS_UNREAD, null))
        assertNull(prefs.getString(LfoStore.KEY_REACTIONS, null))
        assertFalse(prefs.contains(LfoStore.KEY_REACTIONS_HAND))
    }

    @Test
    fun readableRoutesStayWhereTheyAre() {
        val prefs = MemoryPrefs()
        store(prefs, 0).saveReactions("reactions/2", 5)
        assertEquals(5, store(prefs, 5).loadHandMask())
        assertEquals("reactions/2", prefs.getString(LfoStore.KEY_REACTIONS, null))
        assertFalse(prefs.contains(LfoStore.KEY_REACTIONS_UNREAD))
    }
}
