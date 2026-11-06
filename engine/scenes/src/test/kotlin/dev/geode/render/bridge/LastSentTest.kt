package dev.geode.render.bridge

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The live renderer's per-frame values cross JNI on a change only, and all again on a new handle. */
class LastSentTest {
    @Test
    fun aValueGoesOnceUntilItChanges() {
        val sent = LastSent()
        assertTrue(sent.look(0.1f, 0.2f))
        assertFalse(sent.look(0.1f, 0.2f))
        assertTrue(sent.look(0.1f, 0.3f))
        assertTrue(sent.reducedMotion(false))
        assertFalse(sent.reducedMotion(false))
        assertTrue(sent.reducedMotion(true))
        assertTrue(sent.transition("fade", 1200))
        assertFalse(sent.transition("fade", 1200))
        assertTrue(sent.transition("fade", 800))
        assertTrue(sent.transition("cut", 800))
        assertTrue(sent.pacedFps(0f))
        assertFalse(sent.pacedFps(0f))
        assertTrue(sent.pacedFps(30f))
    }

    @Test
    fun clearSendsEveryValueAgain() {
        val sent = LastSent()
        sent.look(0f, 0f)
        sent.reducedMotion(false)
        sent.transition("fade", 1200)
        sent.pacedFps(60f)
        sent.clear()
        assertTrue(sent.look(0f, 0f))
        assertTrue(sent.reducedMotion(false))
        assertTrue(sent.transition("fade", 1200))
        assertTrue(sent.pacedFps(60f))
    }
}
