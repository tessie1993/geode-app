package dev.geode.data

import dev.geode.export.ExportCodec
import dev.geode.export.ExportPresets
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ExportPresetFillTest {
    private val youtube = ExportPresets.ALL.first { it.ratio == ExportRatio.R16_9 && it.quality == ExportQuality.FHD1080 }

    @Test
    fun fillingFromAPlatformSetsOnlyItsFourOptions() {
        val mine =
            ExportDefaults(
                quality = ExportQuality.HD720,
                fps = 24,
                ratio = ExportRatio.R1_1,
                loopSafe = true,
                codec = ExportCodec.HEVC,
                loudnessTargetId = "custom-id",
            )

        val filled = mine.filledFrom(youtube)

        assertEquals(youtube.quality, filled.quality)
        assertEquals(youtube.fps, filled.fps)
        assertEquals(youtube.ratio, filled.ratio)
        assertEquals(youtube.loopSafe, filled.loopSafe)
        assertEquals(ExportCodec.HEVC, filled.codec)
        assertEquals("custom-id", filled.loudnessTargetId)
    }

    @Test
    fun aChangeAfterFillingSticksBecauseThePlatformIsNotRemembered() {
        val filled = ExportDefaults(loudnessTargetId = LoudnessTarget.LeaveAsIs.id).filledFrom(youtube)

        val changed = filled.copy(quality = ExportQuality.HD720)

        // Nothing re-derives the options from a preset: the user's change is the value.
        assertEquals(ExportQuality.HD720, changed.quality)
        assertEquals(youtube.fps, changed.fps)
        assertEquals(youtube.ratio, changed.ratio)
    }

    @Test
    fun everyPlatformFillsAnOfferedCombination() {
        for (preset in ExportPresets.ALL) {
            val filled = ExportDefaults().filledFrom(preset)
            assertTrue("fps ${filled.fps}", filled.fps in EXPORT_FPS_OPTIONS)
            assertTrue(filled.quality in ExportQuality.entries)
            assertTrue(filled.ratio in ExportRatio.entries)
        }
    }
}
