package dev.geode.export

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

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
    fun downsamplingByTwoAveragesPairs() {
        val r = PcmResampler(96_000, 48_000, channels = 2)
        val stereo = shortArrayOf(0, 100, 10, 200, 20, 300, 30, 400)
        assertArrayEquals(shortArrayOf(5, 150, 25, 350), r.process(stereo, frames = 4))
    }

    @Test
    fun oneSecondStaysOneSecondAtEveryRate() {
        for ((from, to) in listOf(96_000 to 48_000, 192_000 to 48_000, 176_400 to 44_100, 44_100 to 48_000, 48_000 to 48_000)) {
            val r = PcmResampler(from, to, channels = 1)
            var out = 0
            // Fed in uneven decoder-sized chunks.
            var left = from
            while (left > 0) {
                val n = minOf(left, 1_152)
                out += r.process(ShortArray(n) { 1_000 }, n).size
                left -= n
            }
            assertTrue("$from -> $to gave $out frames", abs(out - to) <= 1)
        }
    }

    @Test
    fun chunkedInputMatchesOneBuffer() {
        val ramp = ShortArray(1_000) { (it * 7).toShort() }
        for ((from, to) in listOf(96_000 to 48_000, 88_200 to 44_100, 44_100 to 48_000)) {
            val whole = PcmResampler(from, to, channels = 1).process(ramp, ramp.size)
            val chunked = PcmResampler(from, to, channels = 1)
            val pieces = mutableListOf<Short>()
            var at = 0
            for (size in generateSequence(1) { it % 37 + 3 }) {
                if (at >= ramp.size) break
                val n = minOf(size, ramp.size - at)
                pieces += chunked.process(ramp.copyOfRange(at, at + n), n).toList()
                at += n
            }
            assertArrayEquals("$from -> $to", whole, pieces.toShortArray())
        }
    }

    @Test
    fun pcmDurationUsesTheRateTheBytesAreAt() {
        // One second of stereo 16-bit PCM at each rate.
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(44_100L * 4, 44_100, 2))
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(48_000L * 4, 48_000, 2))
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(96_000L * 4, 96_000, 2))
        // A 96 kHz source resampled to 48 kHz: the encoder's bytes are timed at 48k. Timing them at
        // the source rate would halve every timestamp.
        val r = PcmResampler(96_000, 48_000, channels = 2)
        val out = r.process(ShortArray(96_000 * 2), 96_000)
        assertEquals(1_000_000L, PcmResampler.pcmBytesToUs(out.size * 2L, r.outRateHz, 2))
        assertEquals(500_000L, PcmResampler.pcmBytesToUs(out.size * 2L, 96_000, 2))
    }

    @Test
    fun upsamplingInterpolatesBetweenSamples() {
        val r = PcmResampler(24_000, 48_000, channels = 1)
        assertArrayEquals(shortArrayOf(0, 50, 100, 150, 200, 250), r.process(shortArrayOf(0, 100, 200, 300), 4))
    }
}
