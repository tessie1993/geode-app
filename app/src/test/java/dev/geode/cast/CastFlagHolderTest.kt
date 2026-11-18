package dev.geode.cast

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** A stopped cast's render thread ending late must not clear the flags the next cast set. */
class CastFlagHolderTest {
    private val holder = CastFlagHolder()
    private var on = false

    @Test
    fun aStreamClearsTheFlagsItSet() {
        val stream = Any()
        holder.take(stream) { on = true }
        holder.release(stream) { on = false }
        assertFalse(on)
    }

    @Test
    fun anOldStreamEndingAfterTheNextStartedLeavesTheFlagsSet() {
        val old = Any()
        val next = Any()
        holder.take(old) { on = true }
        holder.take(next) { on = true }
        holder.release(old) { on = false }
        assertTrue(on)
        holder.release(next) { on = false }
        assertFalse(on)
    }

    @Test
    fun aStreamThatNeverTookTheFlagsClearsNothing() {
        var clears = 0
        holder.take(Any()) { on = true }
        holder.release(Any()) { clears++ }
        assertEquals(0, clears)
        assertTrue(on)
    }
}
