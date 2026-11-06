package dev.geode.audio

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The record-only tap: the LAST stage of the playback chain ([TapRenderersFactory]), after EQ, ReplayGain,
 * crossfeed and the crossfade gain, so a capture take keeps what the performer heard (ADR 0008). It is the
 * "second tap, downstream" `docs/AUDIO_CHAIN.md` allows: it feeds the take's encoder only, never a sound
 * engine, and the analysis tap stays first.
 *
 * Glue only. Every buffer goes straight to native (`geode_take_write_audio`), which ignores it while no take
 * runs and owns the rest: resampling, placing it on the take's clock, filling gaps. The chain is integer PCM
 * (rule 1 of `AUDIO_CHAIN.md`), so only 16-bit buffers are passed; anything else (a passthrough format) is
 * skipped and the take keeps running on silence.
 *
 * One process-wide instance because there is one playback chain; its buffers arrive on ExoPlayer's playback
 * thread.
 */
@OptIn(UnstableApi::class)
object TakeAudioTap : TeeAudioProcessor.AudioBufferSink {
    @Volatile
    private var sampleRateHz = 0

    @Volatile
    private var channelCount = 0

    @Volatile
    private var pcm16 = false

    /** A direct copy for a heap buffer, which native cannot address; playback thread only. */
    private var direct: ByteBuffer = ByteBuffer.allocateDirect(0)

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        this.sampleRateHz = sampleRateHz
        this.channelCount = channelCount
        pcm16 = encoding == C.ENCODING_PCM_16BIT
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        val channels = channelCount
        if (!pcm16 || channels <= 0 || !buffer.hasRemaining()) return
        if (buffer.isDirect) {
            GeodeNative.takeWriteAudio(buffer, buffer.position(), buffer.remaining(), channels, sampleRateHz)
            return
        }
        if (!GeodeNative.takeActive()) return
        if (direct.capacity() < buffer.remaining()) direct = ByteBuffer.allocateDirect(buffer.remaining()).order(ByteOrder.nativeOrder())
        direct.clear()
        direct.put(buffer.duplicate())
        GeodeNative.takeWriteAudio(direct, 0, direct.position(), channels, sampleRateHz)
    }
}
