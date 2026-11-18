package dev.geode.audio

import android.media.AudioDeviceInfo
import android.os.SystemClock
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioOutput
import androidx.media3.exoplayer.audio.AudioOutputProvider
import dev.geode.engine.bridge.AudioOutNative
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArraySet

/**
 * Media3's [AudioOutput] over the native playback output (`core/audio/output`): an Oboe stream that
 * stands where the AudioTrack stands under `DefaultAudioSink`. Only the handle is held here; each
 * method is one native call, and the FIFO, the position, the volume ramp and the stream's reopening
 * after a route change are native. The audio callback never enters the JVM.
 *
 * The output always plays at 1×: speed and pitch are Sonic's, in the processor chain above. Offload,
 * tunneling and auxiliary effects are the platform track's, and [open] hands such configs back to it.
 *
 * A stream that is lost and will not reopen does not fail playback. The output then reports itself
 * stalled, which makes `DefaultAudioSink` flush and ask the provider for a new output (at the end of
 * the stream, where the sink does not ask, the write throws a recoverable error instead), and [open]
 * answers null for the next minute (native keeps that clock: `core/audio/output/RetryBackoff`), so the
 * platform track takes over and Oboe is tried again later.
 */
@OptIn(UnstableApi::class)
internal class OboeAudioOutput private constructor(
    private var handle: Long,
    private val sampleRateHz: Int,
) : AudioOutput {
    private val listeners = CopyOnWriteArraySet<AudioOutput.Listener>()
    private var failed = false

    override fun play() = AudioOutNative.audioOutPlay(handle)

    override fun pause() = AudioOutNative.audioOutPause(handle)

    override fun write(
        buffer: ByteBuffer,
        encodedAccessUnitCount: Int,
        presentationTimeUs: Long,
    ): Boolean {
        report(AudioOutNative.audioOutEvents(handle))
        val taken = if (failed) -1 else AudioOutNative.audioOutWrite(handle, buffer, buffer.position(), buffer.remaining())
        if (taken < 0) {
            fail()
            // Mid-stream the sink sees the stall and builds a new output. Draining at the end of the
            // stream it never asks, so there the failure is thrown as one a new output recovers from.
            if (presentationTimeUs == C.TIME_END_OF_SOURCE) throw AudioOutput.WriteException(taken, true)
            return false
        }
        buffer.position(buffer.position() + taken)
        return !buffer.hasRemaining()
    }

    override fun flush() = AudioOutNative.audioOutFlush(handle)

    override fun stop() = AudioOutNative.audioOutStop(handle)

    override fun release() {
        if (handle == 0L) return
        AudioOutNative.audioOutRelease(handle)
        // Every native call takes a null handle, so a late call after this is harmless.
        handle = 0L
        listeners.forEach { it.onReleased() }
    }

    override fun setVolume(volume: Float) = AudioOutNative.audioOutSetVolume(handle, volume)

    override fun isOffloadedPlayback(): Boolean = false

    /** The stream's session, which is the player's: `C.AUDIO_SESSION_ID_UNSET` (0) once released. */
    override fun getAudioSessionId(): Int = AudioOutNative.audioOutSessionId(handle)

    override fun getSampleRate(): Int = sampleRateHz

    override fun getBufferSizeInFrames(): Long = AudioOutNative.audioOutBufferFrames(handle)

    override fun getPositionUs(): Long = AudioOutNative.audioOutPositionUs(handle)

    override fun getPlaybackParameters(): PlaybackParameters = PlaybackParameters.DEFAULT

    override fun isStalled(): Boolean = failed || AudioOutNative.audioOutStalled(handle)

    override fun addListener(listener: AudioOutput.Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: AudioOutput.Listener) {
        listeners.remove(listener)
    }

    override fun setPlaybackParameters(playbackParams: PlaybackParameters) = Unit

    override fun setOffloadDelayPadding(
        delayInFrames: Int,
        paddingInFrames: Int,
    ) = Unit

    override fun setOffloadEndOfStream() = Unit

    override fun attachAuxEffect(effectId: Int) = Unit

    override fun setAuxEffectSendLevel(level: Float) = Unit

    override fun setPreferredDevice(preferredDevice: AudioDeviceInfo?) = AudioOutNative.audioOutSetDevice(handle, preferredDevice?.id ?: 0)

    /** Native's event bits (`GEODE_AUDIO_OUT_*`) as Media3's listener calls, on the playback thread. */
    private fun report(events: Int) {
        if (events and AudioOutNative.AUDIO_OUT_ADVANCING != 0) {
            val startedMs = System.currentTimeMillis()
            listeners.forEach { it.onPositionAdvancing(startedMs) }
        }
        if (events and AudioOutNative.AUDIO_OUT_UNDERRUN != 0) listeners.forEach { it.onUnderrun() }
        if (events and AudioOutNative.AUDIO_OUT_FAILED != 0) fail()
    }

    private fun fail() {
        failed = true
        AudioOutNative.audioOutNoteFailed(SystemClock.elapsedRealtime())
    }

    companion object {
        /**
         * An output for [config], or null when the platform track must play it: native's rule
         * (`core/audio/output/OutputChoice`, plus the back-off after a failed stream), or no stream that is
         * exactly the config in the player's audio session.
         */
        fun open(config: AudioOutputProvider.OutputConfig): OboeAudioOutput? {
            val takes =
                AudioOutNative.audioOutTakes(
                    SystemClock.elapsedRealtime(),
                    config.isOffload,
                    config.isTunneling,
                    config.useOffloadGapless,
                    config.usePlaybackParameters,
                    config.virtualDeviceId,
                    config.audioAttributes.usage,
                    config.channelMask,
                    config.encoding,
                    config.sampleRate,
                )
            if (!takes) return null
            val c = config
            val handle = AudioOutNative.audioOutOpen(c.sampleRate, c.channelMask, c.encoding, c.bufferSize, c.audioSessionId)
            return if (handle != 0L) OboeAudioOutput(handle, config.sampleRate) else null
        }
    }
}
