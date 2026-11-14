package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer

/**
 * The player's PCM tap: hands each buffer the audio chain sees to the native [ring] where it lies, with
 * the format it was flushed with. The samples are never read here; the ring converts them to float.
 * Both callbacks run on the playback thread, the ring's one writer while the player is its source.
 * While a capture of another app writes the ring the tap stays out of it, natively (`geode_pcm_ring_hand_over`):
 * a paused player still pushes buffers until its AudioTrack is full.
 */
@OptIn(UnstableApi::class)
class PcmTap(
    private val ring: Long,
) : TeeAudioProcessor.AudioBufferSink {
    private var sampleRateHz = 0
    private var channelCount = 0
    private var encoding = C.ENCODING_INVALID
    private var staging: ByteBuffer? = null

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        // Every flush starts a ring epoch, before a sample of the new format is written (while the tap has it).
        GeodeNative.pcmRingTapFlush(ring)
        this.sampleRateHz = sampleRateHz
        this.channelCount = channelCount
        this.encoding = encoding
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        if (channelCount <= 0 || sampleRateHz <= 0) return
        val block = if (buffer.isDirect) buffer else staged(buffer)
        GeodeNative.pcmRingWriteTap(ring, block, block.position(), block.remaining(), encoding, channelCount, sampleRateHz)
    }

    /** A heap buffer has no address native can read, so its bytes go through a direct one in a single bulk copy. */
    private fun staged(buffer: ByteBuffer): ByteBuffer {
        val bytes = buffer.remaining()
        val copy = staging?.takeIf { it.capacity() >= bytes } ?: ByteBuffer.allocateDirect(bytes).also { staging = it }
        copy.clear()
        copy.put(buffer.duplicate())
        copy.flip()
        return copy
    }
}
