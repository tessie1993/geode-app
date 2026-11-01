package dev.geode.audio.dsp

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A 5.1 or 7.1 file used to fail the sink's configure (the processor threw), and ExoPlayer then
 * called the whole file unplayable. The processor now steps aside for formats its native chain
 * cannot run. These cases never reach the native chain, so they run on the JVM without libgeode.
 */
@OptIn(UnstableApi::class)
class NativeDspProcessorTest {
    @Test
    fun `mono and stereo 16-bit and float run the chain`() {
        listOf(C.ENCODING_PCM_16BIT, C.ENCODING_PCM_FLOAT).forEach { encoding ->
            assertTrue(NativeDspProcessor.runs(encoding, 1))
            assertTrue(NativeDspProcessor.runs(encoding, 2))
        }
    }

    @Test
    fun `more than two channels step aside`() {
        (3..8).forEach { channels ->
            assertFalse("$channels channels", NativeDspProcessor.runs(C.ENCODING_PCM_16BIT, channels))
            assertFalse("$channels channels", NativeDspProcessor.runs(C.ENCODING_PCM_FLOAT, channels))
        }
    }

    @Test
    fun `other encodings step aside`() {
        assertFalse(NativeDspProcessor.runs(C.ENCODING_PCM_24BIT, 2))
        assertFalse(NativeDspProcessor.runs(C.ENCODING_PCM_32BIT, 2))
        assertFalse(NativeDspProcessor.runs(C.ENCODING_PCM_8BIT, 2))
    }

    @Test
    fun `a six-channel stream configures inactive instead of throwing`() {
        val processor = NativeDspProcessor()
        val out = processor.configure(AudioProcessor.AudioFormat(48_000, 6, C.ENCODING_PCM_16BIT))
        assertEquals(AudioProcessor.AudioFormat.NOT_SET, out)
        assertFalse(processor.isActive)
    }
}
