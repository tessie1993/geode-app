package dev.geode.audio

import androidx.annotation.OptIn
import androidx.media3.common.Format
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.common.util.Util
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.ForwardingAudioSink
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer

/**
 * The player's audio sink with its timing read out for the native playout clock (`core/sound/interp/
 * PlayoutClock`, #349, #488), so live visuals hear the player's audio when the speaker plays it. Glue on
 * the playback thread: it passes on what the sink already knows and decides nothing; the delay maths, the
 * speed history and the A/V offset are native.
 *
 * - Each new buffer: its presentation time, the ring position its first sample lands on ([ringWritten]
 *   before the sink takes it; the tap, first in the chain, writes it inside [handleBuffer]) and its frame
 *   count. Reported once per buffer: the sink hands the same buffer again until the AudioTrack takes it
 *   whole, and by then the tap has already written it.
 * - Every [getCurrentPositionUs]: the sink's playback position in media time. Media3 reads it from
 *   `AudioTrack.getTimestamp` (the frame at the DAC, so the HAL and a Bluetooth link are included) and
 *   counts speed and skipped silence in it; the renderer asks about every 10 ms.
 * - [play], [pause], [flush], [reset], and the speed the sink applied, each with the time it happened.
 *
 * A Kotlin file in `:app` beside [TapRenderersFactory], which installs it, rather than in the engine
 * modules, whose Kotlin may only shrink (`tools/engine-kotlin-gate.sh`).
 */
@OptIn(UnstableApi::class)
class PlayoutSink(
    sink: AudioSink,
    private val clock: Long,
    private val ringWritten: () -> Long,
) : ForwardingAudioSink(sink) {
    private var frameBytes = 0

    // The buffer the sink is still taking: already reported to the clock.
    private var midBuffer = false

    override fun configure(
        inputFormat: Format,
        specifiedBufferSize: Int,
        outputChannels: IntArray?,
    ) {
        super.configure(inputFormat, specifiedBufferSize, outputChannels)
        // Encoded passthrough (no tap, no ring) has no PCM frame: no anchors, the clock stays unknown.
        frameBytes =
            if (Util.isEncodingLinearPcm(inputFormat.pcmEncoding)) {
                Util.getPcmFrameSize(inputFormat.pcmEncoding, inputFormat.channelCount)
            } else {
                0
            }
    }

    override fun handleBuffer(
        buffer: ByteBuffer,
        presentationTimeUs: Long,
        encodedAccessUnitCount: Int,
    ): Boolean {
        if (!midBuffer && frameBytes > 0) {
            val frames = (buffer.remaining() / frameBytes).toLong()
            GeodeNative.playoutBufferHandled(clock, presentationTimeUs, ringWritten(), frames)
        }
        val handled = super.handleBuffer(buffer, presentationTimeUs, encodedAccessUnitCount)
        midBuffer = !handled
        return handled
    }

    override fun getCurrentPositionUs(sourceEnded: Boolean): Long {
        val positionUs = super.getCurrentPositionUs(sourceEnded)
        if (positionUs != AudioSink.CURRENT_POSITION_NOT_SET) {
            GeodeNative.playoutPosition(clock, positionUs, System.nanoTime())
        }
        return positionUs
    }

    override fun play() {
        super.play()
        GeodeNative.playoutPlaying(clock, true, System.nanoTime())
    }

    override fun pause() {
        super.pause()
        GeodeNative.playoutPlaying(clock, false, System.nanoTime())
    }

    override fun flush() {
        super.flush()
        midBuffer = false
        GeodeNative.playoutFlush(clock)
    }

    override fun reset() {
        super.reset()
        midBuffer = false
        GeodeNative.playoutPlaying(clock, false, System.nanoTime())
        GeodeNative.playoutFlush(clock)
    }

    override fun setPlaybackParameters(playbackParameters: PlaybackParameters) {
        super.setPlaybackParameters(playbackParameters)
        // What the sink applied, which may be clamped from what was asked.
        GeodeNative.playoutSpeed(clock, super.getPlaybackParameters().speed, System.nanoTime())
    }
}
