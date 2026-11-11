package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/**
 * A touch in the dock restarts an awake chrome's sleep timer, and never wakes a sleeping one; a
 * start of the activity (a wake) brings a hidden or faded chrome back with its timer fresh.
 */
class ShellChromeStateTest {
    @Test
    fun aTouchRestartsTheTimerWhileAwake() {
        val chrome = ShellChromeState()
        val before = chrome.stamp
        chrome.touched()
        assertNotEquals(before, chrome.stamp)
    }

    @Test
    fun aTouchLeavesASleepingChromeAsleep() {
        val chrome = ShellChromeState()
        chrome.sleep()
        val before = chrome.stamp
        chrome.touched()
        assertEquals(before, chrome.stamp)
        assertEquals(false, chrome.awake)
    }

    @Test
    fun aNewChromeStartsAwakeAndCounting() {
        val chrome = ShellChromeState()
        assertEquals(true, chrome.awake)
        assertEquals(true, chrome.counting)
    }

    @Test
    fun aStartBringsBackASwipedAwayChromeWithAFreshTimer() {
        val chrome = ShellChromeState()
        chrome.hide()
        val before = chrome.stamp
        chrome.wake()
        assertEquals(true, chrome.awake)
        assertEquals(true, chrome.counting)
        assertNotEquals(before, chrome.stamp)
    }

    @Test
    fun aStartBringsBackAFadedChromeWithAFreshTimer() {
        val chrome = ShellChromeState()
        chrome.sleep()
        val before = chrome.stamp
        chrome.wake()
        assertEquals(true, chrome.awake)
        assertNotEquals(before, chrome.stamp)
    }

    @Test
    fun theTimerWaitsWhileTheShellIsCovered() {
        val chrome = ShellChromeState()
        chrome.covered = true
        assertEquals(true, chrome.awake)
        assertEquals(false, chrome.counting)
        chrome.covered = false
        assertEquals(true, chrome.counting)
    }

    @Test
    fun aStartInPipStaysAsleepUntilPipEnds() {
        val chrome = ShellChromeState()
        chrome.sleep()
        chrome.held = true
        chrome.wake()
        assertEquals(false, chrome.awake)
        assertEquals(false, chrome.counting)
        chrome.held = false
        assertEquals(true, chrome.awake)
    }
}
