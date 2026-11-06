package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/** Settings › Playback › Crossfade (#338): Off, then 1–12 s in whole seconds. */
class PlayerPrefsCrossfadeTest {
    @Test
    fun offByDefault() {
        assertEquals(0, PlayerPrefs().crossfadeMs)
    }

    @Test
    fun theSavedValueIsClampedToTwelveSeconds() {
        assertEquals(PlayerPrefs.MAX_CROSSFADE_MS, PlayerPrefs(crossfadeMs = 60_000).coerced().crossfadeMs)
        assertEquals(0, PlayerPrefs(crossfadeMs = -500).coerced().crossfadeMs)
        assertEquals(4_000, PlayerPrefs(crossfadeMs = 4_000).coerced().crossfadeMs)
    }

    @Test
    fun theSliderMovesInWholeSecondsBetweenOffAndTwelve() {
        assertEquals(0, PlayerPrefs.crossfadeMsFromSeconds(0f))
        assertEquals(0, PlayerPrefs.crossfadeMsFromSeconds(0.4f))
        assertEquals(1_000, PlayerPrefs.crossfadeMsFromSeconds(0.6f))
        assertEquals(5_000, PlayerPrefs.crossfadeMsFromSeconds(5.2f))
        assertEquals(12_000, PlayerPrefs.crossfadeMsFromSeconds(12f))
    }

    @Test
    fun theSliderNeverLeavesItsRange() {
        assertEquals(12_000, PlayerPrefs.crossfadeMsFromSeconds(13f))
        assertEquals(12_000, PlayerPrefs.crossfadeMsFromSeconds(Float.MAX_VALUE))
        assertEquals(0, PlayerPrefs.crossfadeMsFromSeconds(-3f))
        assertEquals(0, PlayerPrefs.crossfadeMsFromSeconds(Float.NaN))
    }
}
