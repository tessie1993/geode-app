package dev.geode.pro

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NoteResponsePolicyTest {
    private val free = FeatureGate.FREE
    private val trial = FeatureGate(Entitlement.Trial(endsAtMs = 1L))
    private val pro = FeatureGate(Entitlement.Pro)

    @Test
    fun `off is off for everyone`() {
        listOf(free, trial, pro).forEach { assertFalse(NoteResponsePolicy.active(false, it)) }
    }

    @Test
    fun `pro and the trial get it`() {
        listOf(trial, pro).forEach { gate ->
            assertTrue(NoteResponsePolicy.active(true, gate))
            assertFalse(NoteResponsePolicy.isLocked(gate))
        }
    }

    @Test
    fun `free does not, not even a taste`() {
        assertFalse(NoteResponsePolicy.active(true, free))
        assertTrue(NoteResponsePolicy.isLocked(free))
        assertFalse(ProFeature.NOTE_RESPONSE in FreeTier.freeFeatures)
    }
}
