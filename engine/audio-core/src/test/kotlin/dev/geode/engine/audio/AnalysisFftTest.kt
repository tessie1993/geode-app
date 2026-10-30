package dev.geode.engine.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AnalysisFftTest {
    @Test
    fun fftSizeKeepsTheReferenceBinWidth() {
        assertEquals(1024, AnalysisFft.sizeFor(22_050))
        assertEquals(2048, AnalysisFft.sizeFor(44_100))
        assertEquals(2048, AnalysisFft.sizeFor(48_000))
        assertEquals(4096, AnalysisFft.sizeFor(88_200))
        assertEquals(4096, AnalysisFft.sizeFor(96_000))
        assertEquals(8192, AnalysisFft.sizeFor(176_400))
        assertEquals(8192, AnalysisFft.sizeFor(192_000))
        for (rate in listOf(8_000, 16_000, 22_050, 32_000, 44_100, 48_000, 88_200, 96_000, 176_400, 192_000, 384_000)) {
            val binHz = rate.toFloat() / AnalysisFft.sizeFor(rate)
            assertTrue("$rate Hz gives $binHz Hz bins", binHz <= 48_000f / 2048f)
            // And no finer than half the reference: the window stays the same length in time.
            assertTrue("$rate Hz gives $binHz Hz bins", rate < 16_000 || binHz > 48_000f / 4096f)
        }
    }

    @Test
    fun samplesToMillisecondsUseTheStreamRate() {
        assertEquals(1000L, AnalysisFft.sampleToMs(44_100, 44_100))
        assertEquals(1000L, AnalysisFft.sampleToMs(48_000, 48_000))
        assertEquals(1000L, AnalysisFft.sampleToMs(96_000, 96_000))
        // The same sample count is a different time at a different rate.
        assertEquals(918L, AnalysisFft.sampleToMs(44_100, 48_000))
        assertEquals(459L, AnalysisFft.sampleToMs(44_100, 96_000))
        // 30 minutes at 192 kHz does not overflow.
        assertEquals(1_800_000L, AnalysisFft.sampleToMs(192_000L * 1800, 192_000))
    }
}
