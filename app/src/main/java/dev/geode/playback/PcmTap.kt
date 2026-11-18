package dev.geode.playback

import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.bridge.IngestNative
import java.nio.ByteBuffer

/**
 * The player's PCM tap: Media3's [TeeAudioProcessor.AudioBufferSink], which has no native form, so this class only
 * forwards. The format, the epoch at every flush, the gate and the write are native's (`geode_pcm_ring_tap_*`,
 * `core/api/geode_ingest_api.cpp`); no sample is read here. Both callbacks run on the playback thread.
 */
@OptIn(UnstableApi::class)
class PcmTap(
    private val ring: Long,
) : TeeAudioProcessor.AudioBufferSink {
    /** False while a capture of another app writes the ring: the tap then writes nothing. */
    var owns = true
        set(value) {
            field = value
            IngestNative.pcmTapSetOwner(ring, value)
        }

    init {
        // The ring outlives a tap: a new one starts with the gate open, whatever the last left.
        IngestNative.pcmTapSetOwner(ring, true)
    }

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) = IngestNative.pcmTapFlush(ring, sampleRateHz, channelCount, encoding)

    override fun handleBuffer(buffer: ByteBuffer) {
        IngestNative.pcmTapWrite(ring, buffer, buffer.position(), buffer.remaining())
    }
}
