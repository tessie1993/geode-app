package dev.geode.export

import org.junit.Assert.assertEquals
import org.junit.Test

/** The rate policy and timing stay in Kotlin; the conversion itself is tested natively (core/tests/resampler_test.cpp). */
class PcmResamplerTest {
    @Test
    fun aacRateStaysWithinWhatTheEncoderAccepts() {
        assertEquals(22_050, PcmResampler.aacRateFor(22_050))
        assertEquals(44_100, PcmResampler.aacRateFor(44_100))
        assertEquals(48_000, PcmResampler.aacRateFor(48_000))
        assertEquals(44_100, PcmResampler.aacRateFor(88_200))
        assertEquals(48_000, PcmResampler.aacRateFor(96_000))
        assertEquals(44_100, PcmResampler.aacRateFor(176_400))
        assertEquals(48_000, PcmResampler.aacRateFor(192_000))
        assertEquals(48_000, PcmResampler.aacRateFor(64_000))
    }

    @Test
    fun pcmDurationUsesTheRateTheBytesAreAt() {
        // One second of stereo 16-bit PCM at each rate.
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(44_100L * 4, 44_100, 2))
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(48_000L * 4, 48_000, 2))
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(96_000L * 4, 96_000, 2))
        // A 96 kHz source resampled to 48 kHz gives 48000 frames (resampler_test); the encoder's bytes
        // are timed at 48k. Timing them at the source rate would halve every timestamp.
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(48_000L * 4, 48_000, 2))
        assertEquals(500_000L, PcmResampler.pcmBytesToUs(48_000L * 4, 96_000, 2))
    }

    @Test
    fun equalRatesPassThroughWithoutNativeCode() {
        val r = PcmResampler(48_000, 48_000, channels = 2)
        assertEquals(true, r.isPassthrough)
        assertEquals(listOf<Short>(1, 2, 3, 4), r.process(shortArrayOf(1, 2, 3, 4, 5, 6), 2).toList())
        r.close()
    }
}
