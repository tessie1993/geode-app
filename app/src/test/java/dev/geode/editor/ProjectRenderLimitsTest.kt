package dev.geode.editor

import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.model.WatermarkChoice
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * #354: a background project render once came out clean and full length in the trial, because it
 * never read the gate. `ExportController.renderProject` draws `project.cappedAt(limits.capMs)` with
 * `limits.watermark`, the limits being `FeatureGate.renderLimits`; this pins what that gives.
 */
class ProjectRenderLimitsTest {
    private var n = 0
    private val ids = IdSource { "id${n++}" }

    private fun project(lengthMs: Long): StudioProject =
        StudioProject.create(
            id = ProjectId("p"),
            name = "Song",
            destination = ExportDestination.CUSTOM,
            defaults = ExportDefaults(),
            nowMs = 0L,
            ids = ids,
            track = TrackRef(uri = "content://song", durationMs = lengthMs),
        )

    @Test
    fun `a trial render is capped and watermarked, like the one-tap Render`() {
        val limits = FeatureGate(Entitlement.Trial(endsAtMs = 1L)).renderLimits

        val drawn = project(FIVE_MINUTES_MS).cappedAt(limits.capMs)

        assertTrue(limits.watermark.required)
        assertEquals(FreeTier.FREE_MAX_EXPORT_MS, drawn.lengthMs)
        for (clip in drawn.edit.timeline.lanes
            .flatMap { it.clips }) {
            assertEquals(0L, clip.startMs)
            assertEquals(FreeTier.FREE_MAX_EXPORT_MS, clip.durationMs)
        }
    }

    @Test
    fun `a free render is limited the same way`() {
        val limits = FeatureGate.FREE.renderLimits
        assertTrue(limits.watermark.required)
        assertEquals(FreeTier.FREE_MAX_EXPORT_MS, project(FIVE_MINUTES_MS).cappedAt(limits.capMs).lengthMs)
    }

    @Test
    fun `a Pro render is whole and clean`() {
        val limits = FeatureGate(Entitlement.Pro).renderLimits
        val whole = project(FIVE_MINUTES_MS)
        assertFalse(limits.watermark.required)
        assertSame(whole, whole.cappedAt(limits.capMs))
        assertEquals(FIVE_MINUTES_MS, whole.lengthMs)
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

    @Test
    fun `a project shorter than the cap is left as it is`() {
        val short = project(20_000L)
        assertSame(short, short.cappedAt(FreeTier.FREE_MAX_EXPORT_MS))
    }

    private companion object {
        const val FIVE_MINUTES_MS = 300_000L
    }
}
