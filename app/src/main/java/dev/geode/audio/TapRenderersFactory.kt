package dev.geode.audio

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import dev.geode.audio.dsp.MvzAudioProcessorChain
import dev.geode.engine.audioandroid.PcmTap

@OptIn(UnstableApi::class)
class TapRenderersFactory(
    context: Context,
    private val tap: PcmTap,
    private val dsp: List<AudioProcessor> = emptyList(),
    /**
     * The native playout clock the sink reports its timing to ([PlayoutSink], #488), with the position the
     * tap's ring will write next; 0 builds the bare sink (tests, a player no surface hears).
     */
    private val playout: Long = 0L,
    private val ringWritten: () -> Long = { 0L },
) : DefaultRenderersFactory(context) {
    internal fun audioProcessorChain(): MvzAudioProcessorChain =
        MvzAudioProcessorChain(tap, dsp = dsp)

    override fun buildAudioSink(
        context: Context,
        enableFloatOutput: Boolean,
        enableAudioTrackPlaybackParams: Boolean,
    ): AudioSink {
        val built =
            DefaultAudioSink
                .Builder(context)
                .setEnableFloatOutput(enableFloatOutput)
                .setEnableAudioOutputPlaybackParameters(enableAudioTrackPlaybackParams)
                .setAudioProcessorChain(audioProcessorChain())
                .build()
        return if (playout != 0L) PlayoutSink(built, playout, ringWritten) else built
    }
}
