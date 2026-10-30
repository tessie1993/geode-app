package dev.geode.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ThemeStoreTest {
    @Test
    fun aSaveMidSetupDoesNotMarkSetupDone() {
        val store = ThemeStore(MapPrefs())
        store.saveGui(store.loadGui().copy(safetyAcknowledged = true))
        assertFalse(store.loadGui().setupDone)
    }

    @Test
    fun anAnsweredLegacyIntentStillCountsAsSetupDone() {
        val prefs = MapPrefs().apply { values[ThemeStore.KEY_INTENT] = "LISTENING" }
        assertTrue(ThemeStore(prefs).loadGui().setupDone)
    }

    @Test
    fun theLegacyReducedMotionAnswerSeedsTheSettingUntilItIsSaved() {
        val prefs =
            MapPrefs().apply {
                values[ThemeStore.KEY_SAFETY_CHOICE] = ThemeStore.LEGACY_CHOICE_REDUCED_MOTION
            }
        val store = ThemeStore(prefs)
        assertTrue(store.loadGui().reducedMotion)
        store.saveGui(store.loadGui().copy(reducedMotion = false))
        assertFalse(store.loadGui().reducedMotion)
    }
}
