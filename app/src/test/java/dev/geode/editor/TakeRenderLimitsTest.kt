package dev.geode.editor

import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * #354: a take recorded in the trial rendered clean and full length, because the take's background
 * render never read the gate. `ExportController.renderProject` now draws `project.cappedAt(limits.capMs)`
 * with `limits.watermark`, the limits being `FeatureGate.renderLimits`; this pins what that gives.
 */
class TakeRenderLimitsTest {
    private var n = 0
    private val ids = IdSource { "id${n++}" }

    /** A take held through a whole five-minute song, as `TakeStore.save` builds its project. */
    private val fiveMinuteTake =
        StudioProject.fromTake(
            id = ProjectId("p"),
            name = "Take 1",
            take = TakeSpan("t", "neon_chrome_sea", trackStartMs = 0L, durationMs = FIVE_MINUTES_MS),
            track = TrackRef(uri = "content://song", durationMs = FIVE_MINUTES_MS),
            destination = ExportDestination.CUSTOM,
            defaults = ExportDefaults(),
            nowMs = 0L,
            ids = ids,
        )

    @Test
    fun `a trial take renders capped and watermarked, like the one-tap Render`() {
        val limits = FeatureGate(Entitlement.Trial(endsAtMs = 1L)).renderLimits

        val drawn = fiveMinuteTake.cappedAt(limits.capMs)

        assertTrue(limits.watermark)
        assertEquals(FreeTier.FREE_MAX_EXPORT_MS, drawn.lengthMs)
        val lanes = drawn.edit.timeline.lanes
        for (clip in lanes.flatMap { it.clips }) {
            assertEquals(0L, clip.startMs)
            assertEquals(FreeTier.FREE_MAX_EXPORT_MS, clip.durationMs)
        }
        // The song still starts where the take heard it, and the take clip keeps its take id.
        val song =
            lanes
                .single { it.kind == LaneKind.Audio }
                .clips
                .single()
        assertEquals(0L, song.sourceInMs)
        assertEquals(listOf("t"), drawn.takeIds)
    }

    @Test
    fun `a free take is limited the same way`() {
        val limits = FeatureGate.FREE.renderLimits
        assertTrue(limits.watermark)
        assertEquals(FreeTier.FREE_MAX_EXPORT_MS, fiveMinuteTake.cappedAt(limits.capMs).lengthMs)
    }

    @Test
    fun `a Pro take renders whole and clean`() {
        val limits = FeatureGate(Entitlement.Pro).renderLimits
        assertFalse(limits.watermark)
        assertSame(fiveMinuteTake, fiveMinuteTake.cappedAt(limits.capMs))
        assertEquals(FIVE_MINUTES_MS, fiveMinuteTake.lengthMs)
    }

    @Test
    fun `a take shorter than the cap is left as it is`() {
        val short =
            StudioProject.fromTake(
                id = ProjectId("s"),
                name = "Take 2",
                take = TakeSpan("t2", "neon_chrome_sea", trackStartMs = 30_000L, durationMs = 20_000L),
                track = TrackRef(uri = "content://song"),
                destination = ExportDestination.CUSTOM,
                defaults = ExportDefaults(),
                nowMs = 0L,
                ids = ids,
            )
        assertSame(short, short.cappedAt(FreeTier.FREE_MAX_EXPORT_MS))
    }

    private companion object {
        const val FIVE_MINUTES_MS = 300_000L
    }
}
