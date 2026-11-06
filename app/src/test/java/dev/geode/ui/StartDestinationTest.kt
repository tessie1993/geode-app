package dev.geode.ui

import dev.geode.data.BehaviourPrefs
import dev.geode.data.StartScreen
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Settings › Behaviour › Open on (#337): a cold start's destination comes from [BehaviourPrefs]. */
class StartDestinationTest {
    @Test
    fun aChosenScreenIsWhereAColdStartOpens() {
        assertEquals(GeodeDestination.PLAYER, BehaviourPrefs(startScreen = StartScreen.PLAYER).startDestination())
        assertEquals(GeodeDestination.LIBRARY, BehaviourPrefs(startScreen = StartScreen.LIBRARY).startDestination())
        assertEquals(GeodeDestination.VISUALS, BehaviourPrefs(startScreen = StartScreen.VISUALS).startDestination())
        assertEquals(GeodeDestination.STUDIO, BehaviourPrefs(startScreen = StartScreen.STUDIO).startDestination())
    }

    @Test
    fun whereYouLeftOffReopensTheLastScreenShown() {
        val prefs = BehaviourPrefs(startScreen = StartScreen.LAST_USED, lastScreen = StartScreen.VISUALS)
        assertEquals(GeodeDestination.VISUALS, prefs.startDestination())
    }

    @Test
    fun aChosenScreenWinsOverTheLastOneShown() {
        val prefs = BehaviourPrefs(startScreen = StartScreen.LIBRARY, lastScreen = StartScreen.STUDIO)
        assertEquals(GeodeDestination.LIBRARY, prefs.startDestination())
    }

    @Test
    fun aFreshInstallOpensOnThePlayer() {
        assertEquals(GeodeDestination.PLAYER, BehaviourPrefs().startDestination())
    }

    @Test
    fun everyScreenButSettingsIsRecordedAsTheLastOne() {
        assertNull(GeodeDestination.SETTINGS.startScreen)
        GeodeDestination.entries.filter { it != GeodeDestination.SETTINGS }.forEach { destination ->
            val screen = checkNotNull(destination.startScreen)
            // Recorded, then read back as "where you left off", it is the same screen.
            assertEquals(destination, BehaviourPrefs(lastScreen = screen).startDestination())
        }
    }
}
