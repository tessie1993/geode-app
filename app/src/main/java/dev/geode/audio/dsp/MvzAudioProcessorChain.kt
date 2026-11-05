package dev.geode.audio.dsp

import androidx.annotation.OptIn
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessorChain
import androidx.media3.common.audio.SonicAudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.SilenceSkippingAudioProcessor
import dev.geode.engine.audioandroid.PcmTap

@OptIn(UnstableApi::class)
class MvzAudioProcessorChain(
    private val tap: PcmTap,
    dsp: List<AudioProcessor> = emptyList(),
) : AudioProcessorChain {
    private val silenceSkipping = SilenceSkippingAudioProcessor()
    private val sonic = SonicAudioProcessor()

    private val processors: Array<AudioProcessor> =
        (listOf(tap, silenceSkipping, sonic) + dsp).toTypedArray()

    override fun getAudioProcessors(): Array<AudioProcessor> = processors

    override fun applyPlaybackParameters(playbackParameters: PlaybackParameters): PlaybackParameters {
        sonic.setSpeed(playbackParameters.speed)
        sonic.setPitch(playbackParameters.pitch)
        return playbackParameters
    }

    override fun applySkipSilenceEnabled(skipSilenceEnabled: Boolean): Boolean {
        silenceSkipping.setEnabled(skipSilenceEnabled)
        tap.onParametersReapplied() // the sink flushes the chain next: a re-apply, not a seek (#358)
        return skipSilenceEnabled
    }

    override fun getMediaDuration(playoutDuration: Long): Long = sonic.getMediaDuration(playoutDuration)

    override fun getSkippedOutputFrameCount(): Long = silenceSkipping.skippedFrames
}
