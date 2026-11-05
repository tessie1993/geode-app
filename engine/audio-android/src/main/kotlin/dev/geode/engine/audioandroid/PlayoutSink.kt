package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.ForwardingAudioSink
import dev.geode.engine.audio.PlayoutClock
import java.nio.ByteBuffer

/**
 * The player's audio sink with its timing read out for [PlayoutClock] (#349). Pure glue on the playback
 * thread: it passes on what the sink already knows and decides nothing.
 *
 * - After each [handleBuffer], the start time of the buffer being handled. The tap (first in the sink's
 *   chain) begins a new ring epoch inside the sink's flush or reconfiguration, and the buffer the sink
 *   then takes is that epoch's frame 0; the clock keeps the first time it is told per epoch.
 * - Every [getCurrentPositionUs]: the sink's playback position in media time. Media3 reads it from
 *   `AudioTrack.getTimestamp` (the frame at the DAC, so it includes the HAL and a Bluetooth link) and
 *   the renderer asks for it every playback-thread tick, so the clock gets a fresh one about every 10 ms.
 * - [play], [pause], [flush], [reset] and the speed, so it knows when the position moves and how fast.
 *
 * No delay maths here; that is `core/audio/PlayoutClock`.
 */
@OptIn(UnstableApi::class)
class PlayoutSink(
    sink: AudioSink,
    private val clock: PlayoutClock,
) : ForwardingAudioSink(sink) {
    override fun handleBuffer(
        buffer: ByteBuffer,
        presentationTimeUs: Long,
        encodedAccessUnitCount: Int,
    ): Boolean {
        val handled = super.handleBuffer(buffer, presentationTimeUs, encodedAccessUnitCount)
        clock.bufferHandled(presentationTimeUs)
        return handled
    }

    override fun getCurrentPositionUs(sourceEnded: Boolean): Long {
        val positionUs = super.getCurrentPositionUs(sourceEnded)
        if (positionUs != AudioSink.CURRENT_POSITION_NOT_SET) clock.position(positionUs)
        return positionUs
    }

    override fun play() {
        super.play()
        clock.playing(true)
    }

    override fun pause() {
        super.pause()
        clock.playing(false)
    }

    override fun flush() {
        super.flush()
        clock.flush()
    }

    override fun reset() {
        super.reset()
        clock.playing(false)
        clock.flush()
    }

    override fun setPlaybackParameters(playbackParameters: PlaybackParameters) {
        super.setPlaybackParameters(playbackParameters)
        // What the sink applied, which may be clamped from what was asked.
        clock.speed(super.getPlaybackParameters().speed)
    }
}
