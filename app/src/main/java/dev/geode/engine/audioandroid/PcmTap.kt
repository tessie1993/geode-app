package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.bridge.IngestNative
import java.nio.ByteBuffer

/**
 * The player's PCM tap: hands each buffer the audio chain sees to the native [ring] where it lies, with
 * the format it was flushed with. The samples are never read here; the ring converts them to float.
 * Both callbacks run on the playback thread, the ring's one writer while the player is its source.
 * While a capture of another app writes the ring, [owns] is false and the tap stays out of it: a paused
 * player still pushes buffers until its AudioTrack is full.
 */
@OptIn(UnstableApi::class)
class PcmTap(
    private val ring: Long,
) : TeeAudioProcessor.AudioBufferSink {
    private var sampleRateHz = 0
    private var channelCount = 0
    private var encoding = C.ENCODING_INVALID

    @Volatile
    var owns = true

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        // Every flush starts a ring epoch, before a sample of the new format is written.
        if (owns) IngestNative.pcmRingBeginEpoch(ring)
        this.sampleRateHz = sampleRateHz
        this.channelCount = channelCount
        this.encoding = encoding
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        if (!owns || channelCount <= 0 || sampleRateHz <= 0) return
        IngestNative.pcmRingWriteDirect(ring, buffer, buffer.position(), buffer.remaining(), encoding, channelCount, sampleRateHz)
    }
}
