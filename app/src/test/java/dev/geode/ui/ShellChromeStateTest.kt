package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

/** A touch in the dock restarts an awake chrome's sleep timer, and never wakes a sleeping one. */
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
}
