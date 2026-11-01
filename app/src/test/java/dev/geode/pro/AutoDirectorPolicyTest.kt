package dev.geode.pro

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AutoDirectorPolicyTest {
    private val free = FeatureGate.FREE
    private val trial = FeatureGate(Entitlement.Trial(endsAtMs = 1L))
    private val pro = FeatureGate(Entitlement.Pro)

    @Test
    fun `off is off for everyone`() {
        listOf(free, trial, pro).forEach { assertEquals(0f, AutoDirectorPolicy.amount(false, it, 0L), 0f) }
    }

    @Test
    fun `pro and the trial direct the whole song`() {
        listOf(trial, pro).forEach { gate ->
            assertEquals(1f, AutoDirectorPolicy.amount(true, gate, 10L * 60_000L), 0f)
            assertFalse(AutoDirectorPolicy.isTaste(gate))
        }
    }

    @Test
    fun `free gets the first minute of each song`() {
        assertTrue(AutoDirectorPolicy.isTaste(free))
        assertEquals(1f, AutoDirectorPolicy.amount(true, free, 0L), 0f)
        assertEquals(1f, AutoDirectorPolicy.amount(true, free, FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS - 1), 0f)
        assertEquals(0f, AutoDirectorPolicy.amount(true, free, FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS), 0f)
    }

    @Test
    fun `the taste covers a whole free export`() {
        assertTrue(FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS >= FreeTier.FREE_MAX_EXPORT_MS)
    }
}
