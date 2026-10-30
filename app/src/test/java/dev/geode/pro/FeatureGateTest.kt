package dev.geode.pro

import dev.geode.billing.CachedEntitlement
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FeatureGateTest {
    private val free = FeatureGate.FREE
    private val trial = FeatureGate(Entitlement.Trial(endsAtMs = 1L))
    private val pro = FeatureGate(Entitlement.Pro)

    @Test
    fun `free allows only the free tier`() {
        ProFeature.entries.forEach { feature ->
            assertEquals(feature.name, feature in FreeTier.freeFeatures, free.allows(feature))
        }
        FreeTier.freeStyleIds.forEach { assertTrue(it, free.allowsStyle(it)) }
        assertFalse(free.allowsStyle("not-a-free-style"))
        assertTrue(free.watermarksExports)
        assertEquals(FreeTier.FREE_MAX_EXPORT_MS, free.exportCapMs)
    }

    @Test
    fun `pro allows everything`() {
        ProFeature.entries.forEach { assertTrue(it.name, pro.allows(it)) }
        assertTrue(pro.allowsStyle("not-a-free-style"))
        assertFalse(pro.watermarksExports)
        assertNull(pro.exportCapMs)
    }

    @Test
    fun `the trial allows everything except a clean, full-length export`() {
        val exportLimits = setOf(ProFeature.NO_WATERMARK, ProFeature.FULL_LENGTH_EXPORT)
        ProFeature.entries.filter { it !in exportLimits }.forEach { assertTrue(it.name, trial.allows(it)) }
        exportLimits.forEach { assertEquals(it.name, !FreeTier.TRIAL_EXPORTS_LIMITED, trial.allows(it)) }
        assertEquals(FreeTier.TRIAL_EXPORTS_LIMITED, trial.watermarksExports)
        assertEquals(if (FreeTier.TRIAL_EXPORTS_LIMITED) FreeTier.FREE_MAX_EXPORT_MS else null, trial.exportCapMs)
    }

    @Test
    fun `the free styles are the shader family and include the fallback`() {
        assertTrue(FreeTier.FALLBACK_STYLE_ID in FreeTier.freeStyleIds)
        assertTrue(free.allowsStyle(FreeTier.FALLBACK_STYLE_ID))
        assertFalse("silk is a Pro renderer", free.allowsStyle(SceneIds.DEFAULT))
    }

    @Test
    fun `each style family gives away its first style and keeps the rest for Pro`() {
        StyleFamilies.all.forEach { family ->
            assertTrue(family.freeId, free.allowsStyle(family.freeId))
            family.proIds.forEach { id ->
                assertFalse(id, free.allowsStyle(id))
                assertTrue(id, trial.allowsStyle(id))
                assertTrue(id, pro.allowsStyle(id))
            }
        }
    }

    @Test
    fun `exports above 1080p or 30 fps are blocked for free, resolution first`() {
        assertNull(free.blockingExportFeature(shortSide = 1080, fps = 30))
        assertNull(free.blockingExportFeature(shortSide = 720, fps = 24))
        assertEquals(ProFeature.EXPORT_60FPS, free.blockingExportFeature(shortSide = 1080, fps = 60))
        assertEquals(ProFeature.EXPORT_4K, free.blockingExportFeature(shortSide = 2160, fps = 30))
        assertEquals(ProFeature.EXPORT_4K, free.blockingExportFeature(shortSide = 2160, fps = 60))
        assertNull(pro.blockingExportFeature(shortSide = 2160, fps = 60))
        assertNull(trial.blockingExportFeature(shortSide = 2160, fps = 60))
    }

    @Test
    fun `the trial is an hour of wall clock and a clock moved back ends it`() {
        assertEquals(TrialState.Available, TrialState.at(startedAtMs = null, endNoticeShown = false, nowMs = 5L))
        val hour = FreeTier.TRIAL_DURATION_MS
        assertEquals(TrialState.Active(1_000L + hour), TrialState.at(1_000L, false, 1_000L))
        assertEquals(TrialState.Active(1_000L + hour), TrialState.at(1_000L, false, 1_000L + hour - 1))
        assertEquals(TrialState.Ended(false), TrialState.at(1_000L, false, 1_000L + hour))
        assertEquals(TrialState.Ended(true), TrialState.at(1_000L, true, 999L))
    }

    @Test
    fun `the cached entitlement vouches for a month, and never against a clock moved back`() {
        val confirmed = CachedEntitlement(owned = true, verifiedAtMs = 10_000L)
        assertTrue(confirmed.vouchesForPro(10_000L))
        assertTrue(confirmed.vouchesForPro(10_000L + CachedEntitlement.GRACE_MS - 1))
        assertFalse(confirmed.vouchesForPro(10_000L + CachedEntitlement.GRACE_MS))
        assertTrue(confirmed.vouchesForPro(0L))
        assertFalse(CachedEntitlement.NONE.vouchesForPro(0L))
    }
}
