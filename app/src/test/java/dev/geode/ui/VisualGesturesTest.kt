package dev.geode.ui

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI

/** The pure decisions of the visual's gesture detector (#556). */
class VisualGesturesTest {
    private val window = Rect(0f, 0f, 1080f, 2400f)

    @Test
    fun theSafeAreaIsTheWindowLessEachStrip() {
        assertEquals(Rect(40f, 100f, 1040f, 2350f), gestureSafeArea(window, left = 40f, top = 100f, right = 40f, bottom = 50f))
    }

    @Test
    fun aDownInASideStripIsTheSystemsEvenWhenTheStageIsOffset() {
        // The expanded shell puts the stage beside an 80 px rail: a stage-local x of 10 is window
        // x 90, outside a 40 px back strip, while a stage at the window's edge would be inside it.
        val safe = gestureSafeArea(window, left = 40f, top = 0f, right = 40f, bottom = 0f)
        val beside = WindowPlacement().apply { origin = Offset(80f, 0f) }
        val atEdge = WindowPlacement()
        assertTrue(safe.contains(beside.toWindow(Offset(10f, 1200f))))
        assertFalse(safe.contains(atEdge.toWindow(Offset(10f, 1200f))))
        assertFalse(safe.contains(atEdge.toWindow(Offset(1060f, 1200f))))
    }

    @Test
    fun aDownInTheBottomStripIsTheSystems() {
        val safe = gestureSafeArea(window, left = 0f, top = 0f, right = 0f, bottom = 60f)
        assertTrue(safe.contains(Offset(540f, 2300f)))
        assertFalse(safe.contains(Offset(540f, 2350f)))
    }

    @Test
    fun aSecondTapCountsOnlyWithinTheDoubleTapSlop() {
        assertTrue(isSecondTap(Offset(30f, 40f), doubleTapSlopPx = 50f))
        assertTrue(isSecondTap(Offset.Zero, doubleTapSlopPx = 50f))
        assertFalse(isSecondTap(Offset(30f, 41f), doubleTapSlopPx = 50f))
        assertFalse(isSecondTap(Offset(-400f, 0f), doubleTapSlopPx = 50f))
    }

    @Test
    fun pastHalfACircleIsASwirlAttemptEitherWay() {
        val pi = PI.toFloat()
        assertFalse(swirlAttempted(0f))
        assertFalse(swirlAttempted(pi / 2f))
        assertFalse(swirlAttempted(pi * 0.95f))
        assertTrue(swirlAttempted(pi * 1.05f))
        assertTrue(swirlAttempted(-pi * 1.05f))
        assertTrue(swirlAttempted(3.6f * pi))
    }
}
