package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/** The search overlay belongs to the screen it opened over: any change of destination closes it. */
class GeodeAppStateTest {
    @Test
    fun navigatingClosesSearch() {
        val state = GeodeAppState(searching = true)
        state.navigateTo(GeodeDestination.VISUALS)
        assertFalse(state.searching)
        assertEquals(GeodeDestination.VISUALS, state.dest)
    }

    @Test
    fun everyShortcutToAScreenClosesSearch() {
        val shortcuts: List<(GeodeAppState) -> Unit> =
            listOf(
                GeodeAppState::openSettingsFolders,
                GeodeAppState::openStudioRender,
                GeodeAppState::resetToPlayer,
            )
        for (go in shortcuts) {
            val state = GeodeAppState(dest = GeodeDestination.LIBRARY, searching = true)
            go(state)
            assertFalse(state.searching)
        }
    }
}
