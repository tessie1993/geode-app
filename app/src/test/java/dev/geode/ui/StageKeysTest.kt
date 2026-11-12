package dev.geode.ui

import android.view.KeyEvent
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The remote's twins of the visual's gestures (Android TV): which gesture each key stands for. */
class StageKeysTest {
    @Test
    fun fullScreenTheDpadStepsLikeASwipeRightBeingNext() {
        assertEquals((CanvasGesture.SWIPE_SIDEWAYS to 1), stageKeyGesture(KeyEvent.KEYCODE_DPAD_RIGHT, optionsShown = false))
        assertEquals((CanvasGesture.SWIPE_SIDEWAYS to -1), stageKeyGesture(KeyEvent.KEYCODE_DPAD_LEFT, optionsShown = false))
        assertEquals((CanvasGesture.SWIPE_UP to 0), stageKeyGesture(KeyEvent.KEYCODE_DPAD_UP, optionsShown = false))
        assertEquals((CanvasGesture.SWIPE_DOWN to 0), stageKeyGesture(KeyEvent.KEYCODE_DPAD_DOWN, optionsShown = false))
    }

    @Test
    fun withTheOptionsUpTheArrowsAreLeftToFocus() {
        listOf(
            KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_DPAD_DOWN,
        ).forEach { assertNull(stageKeyGesture(it, optionsShown = true)) }
    }

    @Test
    fun theCentreIsTheDoubleTapEitherWay() {
        listOf(KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_BUTTON_A).forEach {
            assertEquals((CanvasGesture.DOUBLE_TAP to 0), stageKeyGesture(it, optionsShown = false))
            assertEquals((CanvasGesture.DOUBLE_TAP to 0), stageKeyGesture(it, optionsShown = true))
        }
    }

    @Test
    fun channelKeysStepEvenWithTheOptionsUp() {
        assertEquals((CanvasGesture.SWIPE_SIDEWAYS to 1), stageKeyGesture(KeyEvent.KEYCODE_CHANNEL_UP, optionsShown = true))
        assertEquals((CanvasGesture.SWIPE_SIDEWAYS to -1), stageKeyGesture(KeyEvent.KEYCODE_CHANNEL_DOWN, optionsShown = true))
    }

    @Test
    fun backAndTheMediaKeysAreNoGesture() {
        listOf(
            KeyEvent.KEYCODE_BACK,
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
            KeyEvent.KEYCODE_MEDIA_NEXT,
            KeyEvent.KEYCODE_MEDIA_PREVIOUS,
            KeyEvent.KEYCODE_HOME,
        ).forEach {
            assertNull(stageKeyGesture(it, optionsShown = false))
            assertNull(stageKeyGesture(it, optionsShown = true))
        }
    }

    /** Every action the owner's default map gives a gesture is reachable from some key. */
    @Test
    fun everyDefaultActionHasAKey() {
        // Past every key code Android defines; KeyEvent.getMaxKeyCode() is a stub in a JVM test.
        val keys = (0..1024).mapNotNull { stageKeyGesture(it, optionsShown = false) }
        val reached = keys.map { BehaviourPrefs().actionFor(it.first) }.toSet()
        val wanted = BehaviourPrefs.DEFAULT_GESTURES.values.toSet() - GestureAction.OFF
        assertTrue("no key for ${wanted - reached}", reached.containsAll(wanted))
    }
}
