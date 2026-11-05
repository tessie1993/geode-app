package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer

/**
 * The player's PCM tap, first in `MvzAudioProcessorChain`: every buffer the player decodes, before any DSP.
 *
 * It touches no sample (#481, ADR 0003). [handleBuffer] hands the tap's buffer straight to the native
 * ingest ring ([ingest], `core/audio/ingest`), which converts int16, u8 or float, keeps every channel with
 * its layout and gives each renderer the stereo downmix through its own cursor. [flush] begins the ring's
 * new epoch here, on the playback thread, before the first sample of the new stream is written, so a
 * reader can never pair those samples with the old rate (#361). Every flush is an epoch for now, as it was:
 * telling a speed or pitch flush (`geode_ingest_set_format`, no restart) from a seek is #487.
 */
@OptIn(UnstableApi::class)
class PcmTap(
    /** The native ingest ring (`geode_ingest*`); it outlives the player (see `PlaybackEngine.playerIngest`). */
    private val ingest: Long,
    private val onFormat: (PcmTapFormat) -> Unit = {},
) : TeeAudioProcessor.AudioBufferSink {
    @Volatile
    var format: PcmTapFormat? = null
        private set

    @Volatile
    var framesWritten: Long = 0L
        private set

    /** Only for a heap buffer, which JNI cannot address; Media3's sink buffers are direct. One bulk copy. */
    private var bounce: ByteBuffer? = null

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        val next =
            PcmTapFormat(
                sampleRateHz = sampleRateHz,
                channelCount = channelCount,
                encoding = encoding,
                generation = (format?.generation ?: 0) + 1,
            )
        framesWritten = 0L
        format = next
        // Mask 0: the default layout for the count (the tap is told no mask).
        GeodeNative.ingestBeginEpoch(ingest, sampleRateHz, channelCount, 0)
        onFormat(next)
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        val active = format ?: return
        val length = buffer.remaining()
        if (length <= 0) return
        val bytes = if (buffer.isDirect) buffer else bounced(buffer)
        framesWritten += GeodeNative.ingestWrite(ingest, bytes, bytes.position(), length, active.encoding)
    }

    private fun bounced(buffer: ByteBuffer): ByteBuffer {
        val length = buffer.remaining()
        val direct = bounce?.takeIf { it.capacity() >= length } ?: ByteBuffer.allocateDirect(length).also { bounce = it }
        direct.clear()
        direct.put(buffer.duplicate())
        direct.flip()
        return direct
    }
}
