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
        listOf(free, trial, pro).forEach {
            assertEquals(0f, AutoDirectorPolicy.amount(false, it, 0L, playerPlaying = true), 0f)
        }
    }

    @Test
    fun `pro and the trial direct the whole song`() {
        listOf(trial, pro).forEach { gate ->
            assertEquals(1f, AutoDirectorPolicy.amount(true, gate, 10L * 60_000L, playerPlaying = true), 0f)
            assertFalse(AutoDirectorPolicy.isTaste(gate))
        }
    }

    @Test
    fun `pro keeps directing the mic and other apps' audio`() {
        listOf(trial, pro).forEach { gate ->
            assertEquals(1f, AutoDirectorPolicy.amount(true, gate, 0L, playerPlaying = false), 0f)
        }
    }

    @Test
    fun `free gets the first minute of each song`() {
        assertTrue(AutoDirectorPolicy.isTaste(free))
        assertEquals(1f, AutoDirectorPolicy.amount(true, free, 0L, playerPlaying = true), 0f)
        val lastMs = FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS - 1
        assertEquals(1f, AutoDirectorPolicy.amount(true, free, lastMs, playerPlaying = true), 0f)
        val endMs = FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS
        assertEquals(0f, AutoDirectorPolicy.amount(true, free, endMs, playerPlaying = true), 0f)
    }

    @Test
    fun `free gets no taste when the player is not the source`() {
        // Capture pauses the player, so the position stops at 0 and would otherwise never leave the taste.
        assertEquals(0f, AutoDirectorPolicy.amount(true, free, 0L, playerPlaying = false), 0f)
    }

    @Test
    fun `the taste covers a whole free export and matches the native one`() {
        // An export's director is decided natively (director::forExport, core/tests/resolver_test) against
        // director::kFreeTasteMs, which must stay this value.
        assertTrue(FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS >= FreeTier.FREE_MAX_EXPORT_MS)
        assertEquals(60_000L, FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS)
    }
}
