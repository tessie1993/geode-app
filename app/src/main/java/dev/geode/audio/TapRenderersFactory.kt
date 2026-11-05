package dev.geode.audio

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.audio.dsp.MvzAudioProcessorChain
import dev.geode.engine.audio.PlayoutClock
import dev.geode.engine.audioandroid.PlayoutSink
import dev.geode.engine.audioandroid.SinkClockHooks

@OptIn(UnstableApi::class)
class TapRenderersFactory(
    context: Context,
    private val sink: TeeAudioProcessor.AudioBufferSink,
    private val hooks: SinkClockHooks = SinkClockHooks.None,
    private val dsp: List<AudioProcessor> = emptyList(),
    /** When set, the sink reports its timing to it, so live visuals wait for the speaker (#349). */
    private val playout: PlayoutClock? = null,
) : DefaultRenderersFactory(context) {
    internal fun audioProcessorChain(): MvzAudioProcessorChain =
        MvzAudioProcessorChain(TeeAudioProcessor(sink), dsp = dsp, hooks = hooks).also(hooks::attachSkippedFrames)

    override fun buildAudioSink(
        context: Context,
        enableFloatOutput: Boolean,
        enableAudioTrackPlaybackParams: Boolean,
    ): AudioSink {
        val sink =
            DefaultAudioSink
                .Builder(context)
                .setEnableFloatOutput(enableFloatOutput)
                .setEnableAudioOutputPlaybackParameters(enableAudioTrackPlaybackParams)
                .setAudioProcessorChain(audioProcessorChain())
                .build()
        return playout?.let { PlayoutSink(sink, it) } ?: sink
    }
}
