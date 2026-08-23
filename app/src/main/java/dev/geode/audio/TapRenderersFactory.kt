package dev.geode.audio

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.audio.dsp.MvzAudioProcessorChain
import dev.geode.engine.audioandroid.SinkClockHooks

@OptIn(UnstableApi::class)
class TapRenderersFactory(
    context: Context,
    private val sink: TeeAudioProcessor.AudioBufferSink,
    private val hooks: SinkClockHooks = SinkClockHooks.None,
) : DefaultRenderersFactory(context) {
    internal fun audioProcessorChain(): MvzAudioProcessorChain =
        MvzAudioProcessorChain(TeeAudioProcessor(sink), hooks = hooks).also(hooks::attachSkippedFrames)

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
            .build()
}
