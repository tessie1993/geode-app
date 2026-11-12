package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Only the passthrough is Kotlin. The conversion is tested natively (core/tests/resampler_test.cpp), and the
 * AAC rate policy and s16 timing moved to core/audio/dsp/PcmTiming with their cases (core/tests/export_pcm_test.cpp):
 * JVM tests have no native library.
 */
class PcmResamplerTest {
    @Test
    fun equalRatesPassThroughWithoutNativeCode() {
        val r = PcmResampler(48_000, 48_000, channels = 2)
        assertEquals(true, r.isPassthrough)
        assertEquals(listOf<Short>(1, 2, 3, 4), r.process(shortArrayOf(1, 2, 3, 4, 5, 6), 2).toList())
        r.close()
    }
}
