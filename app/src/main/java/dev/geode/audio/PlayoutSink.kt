package dev.geode.audio

import androidx.annotation.OptIn
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.ForwardingAudioSink
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer

/**
 * The player's sink, telling the native playout clock ([clock], `core/sound/interp/PlayoutClock`) what
 * it does: which media time each buffer the tap wrote to [ring] carries, where the output stands, and
 * every play, pause, flush and speed change. The clock answers which ring sample the speaker plays now,
 * and each live renderer reads the ring only that far (`geode_viz_feed_ring`), so the picture moves when
 * the sound is heard, not when the sink took it. Values across, nothing decided here.
 */
@OptIn(UnstableApi::class)
internal class PlayoutSink(
    sink: AudioSink,
    private val clock: Long,
    private val ring: SampleRing,
) : ForwardingAudioSink(sink) {
    override fun handleBuffer(
        buffer: ByteBuffer,
        presentationTimeUs: Long,
        encodedAccessUnitCount: Int,
    ): Boolean {
        val before = GeodeNative.pcmRingHead(ring.handle)
        val handled = super.handleBuffer(buffer, presentationTimeUs, encodedAccessUnitCount)
        val written = GeodeNative.pcmRingHead(ring.handle) - before
        GeodeNative.playoutBufferHandled(clock, presentationTimeUs, before, written)
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
        GeodeNative.playoutFlush(clock)
    }

    override fun reset() {
        super.reset()
        GeodeNative.playoutPlaying(clock, false, System.nanoTime())
        GeodeNative.playoutFlush(clock)
    }

    override fun setPlaybackParameters(playbackParameters: PlaybackParameters) {
        super.setPlaybackParameters(playbackParameters)
        GeodeNative.playoutSpeed(clock, getPlaybackParameters().speed)
    }
}
