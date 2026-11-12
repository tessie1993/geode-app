package dev.geode.pro

import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.WatermarkChoice
import dev.geode.render.scene.StyleCatalogue
import dev.geode.render.scene.TestCatalogue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FeatureGateTest {
    init {
        TestCatalogue.install()
    }

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
        assertFalse("silk is a Pro renderer", free.allowsStyle(StyleCatalogue.DEFAULT_ID))
    }

    @Test
    fun `each style family gives away its first style and keeps the rest for Pro`() {
        TestCatalogue.OPENERS.forEach { assertTrue(it, free.allowsStyle(it)) }
        val followers = StyleCatalogue.ids(GeodeNative.STYLE_FAMILY_FOLLOWER)
        assertTrue(followers.isNotEmpty())
        followers.forEach { id ->
            assertFalse(id, free.allowsStyle(id))
            assertTrue(id, trial.allowsStyle(id))
            assertTrue(id, pro.allowsStyle(id))
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
    fun `a clock moved back inside the hour does not replay the trial`() {
        val hour = FreeTier.TRIAL_DURATION_MS
        val slack = FreeTier.TRIAL_CLOCK_SLACK_MS
        val start = 1_000L
        val seenAt50 = start + hour * 5 / 6

        assertEquals("back to minute 1", TrialState.Ended(false), TrialState.at(start, false, start + 60_000L, seenAt50))
        assertEquals("just past the slack", TrialState.Ended(false), TrialState.at(start, false, seenAt50 - slack - 1, seenAt50))
        assertEquals("a time correction", TrialState.Active(start + hour), TrialState.at(start, false, seenAt50 - slack, seenAt50))
        assertEquals(
            "inside the slack the trial reads at the latest time seen, so stepping back gains nothing",
            TrialState.Ended(false),
            TrialState.at(start, false, start + hour - 1, start + hour),
        )
    }

    @Test
    fun `an ended trial is remembered as ended whatever the clock says later`() {
        val hour = FreeTier.TRIAL_DURATION_MS
        val start = 1_000L
        val running = TrialState.Active(start + hour)
        assertEquals(start + 5L, TrialState.lastSeenAfter(running, start, nowMs = start + 5L, lastSeenMs = 0L))
        assertEquals("never earlier than before", start + 9L, TrialState.lastSeenAfter(running, start, start + 5L, start + 9L))

        // Ended by a clock set before the start: the mark still moves to the trial's end.
        val mark = TrialState.lastSeenAfter(TrialState.Ended(false), start, nowMs = 10L, lastSeenMs = start + 5L)
        assertEquals(start + hour, mark)
        assertEquals(TrialState.Ended(false), TrialState.at(start, false, start + 60_000L, mark))
        assertEquals(TrialState.Ended(false), TrialState.at(start, false, start + hour - 1, mark))
    }

    @Test
    fun `the neon set gives away one opener and keeps four for Pro`() {
        assertTrue(FreeTier.neonOpener in FreeTier.freeStyleIds)
        assertEquals(4, FreeTier.neonProIds.size)
        FreeTier.neonProIds.forEach { assertFalse(it, it in FreeTier.freeStyleIds) }
        assertFalse(FreeTier.neonOpener in FreeTier.neonProIds)
    }

    @Test
    fun `the watermark preference places the mark and never lifts a required one`() {
        val preference =
            WatermarkChoice(required = false, proAdds = true, placement = WatermarkChoice.CORNER, corner = WatermarkChoice.TOP_LEFT)
        val free = FeatureGate.FREE.renderLimits.withMark(preference)
        assertTrue(free.watermark.required)
        assertEquals(WatermarkChoice.CORNER, free.watermark.placement)
        assertEquals(WatermarkChoice.TOP_LEFT, free.watermark.corner)
        val pro = FeatureGate(Entitlement.Pro).renderLimits.withMark(preference)
        assertFalse(pro.watermark.required)
        assertTrue(pro.watermark.proAdds)
        assertEquals(WatermarkChoice.TILED, FeatureGate.FREE.renderLimits.watermark.placement)
    }
}
