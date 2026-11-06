package dev.geode.audio

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.audio.AudioOutput
import androidx.media3.exoplayer.audio.AudioOutputProvider
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.AudioTrackAudioOutputProvider
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.audio.ForwardingAudioOutputProvider
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.audio.dsp.MvzAudioProcessorChain
import dev.geode.engine.audioandroid.SinkClockHooks

/**
 * [onAudioOutput] hears each output's config (rate, channel mask, encoding) on the playback thread just
 * before its AudioTrack is built: the one moment a platform choice that is fixed at track creation can
 * still be made, which `playback/BitPerfectOutput` needs (issue #353). The tap's format callback, which
 * it used before, runs only after Media3 has opened the first track.
 */
@OptIn(UnstableApi::class)
class TapRenderersFactory(
    context: Context,
    private val sink: TeeAudioProcessor.AudioBufferSink,
    private val hooks: SinkClockHooks = SinkClockHooks.None,
    private val dsp: List<AudioProcessor> = emptyList(),
    private val onAudioOutput: (AudioOutputProvider.OutputConfig) -> Unit = {},
) : DefaultRenderersFactory(context) {
    /**
     * The analysis tap first, the user's DSP after it, and the record-only [TakeAudioTap] last of all, so a
     * capture take hears every stage the speaker does (`docs/AUDIO_CHAIN.md`).
     */
    internal fun audioProcessorChain(): MvzAudioProcessorChain =
        MvzAudioProcessorChain(
            TeeAudioProcessor(sink),
            dsp = dsp + TeeAudioProcessor(TakeAudioTap),
            hooks = hooks,
        ).also(hooks::attachSkippedFrames)

    override fun buildAudioSink(
        context: Context,
        enableFloatOutput: Boolean,
        enableAudioTrackPlaybackParams: Boolean,
    ): AudioSink =
        DefaultAudioSink
            .Builder(context)
            .setEnableFloatOutput(enableFloatOutput)
            .setEnableAudioOutputPlaybackParameters(enableAudioTrackPlaybackParams)
            .setAudioProcessorChain(audioProcessorChain())
            .setAudioOutputProvider(
                // The provider DefaultAudioSink builds by default, with a look at each config first.
                object : ForwardingAudioOutputProvider(AudioTrackAudioOutputProvider.Builder(context).build()) {
                    override fun getAudioOutput(config: AudioOutputProvider.OutputConfig): AudioOutput {
                        onAudioOutput(config)
                        return super.getAudioOutput(config)
                    }
                },
            ).build()
}
