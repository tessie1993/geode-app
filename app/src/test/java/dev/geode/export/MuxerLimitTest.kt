package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MuxerLimitTest {
    @Test
    fun shortRendersKeepTheirBitRate() {
        assertEquals(40_000_000, VideoExporter.bitRateUnderMuxerLimit(40_000_000, 5 * 60_000_000L))
    }

    @Test
    fun longRendersFitUnderFourGigabytes() {
        val threeHoursUs = 3L * 3600 * 1_000_000
        val rate = VideoExporter.bitRateUnderMuxerLimit(12_000_000, threeHoursUs)
        assertTrue(rate < 12_000_000)
        val bytes = (rate + 192_000L) * (threeHoursUs / 1_000_000L) / 8
        assertTrue(bytes < LoopExtend.MUXER_SAFE_BYTES)
    }
}
