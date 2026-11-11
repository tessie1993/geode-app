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
import dev.geode.engine.audio.SampleRing

/**
 * The player's renderers, with the tap first in the audio chain and the choice of what plays the
 * sink's PCM: the platform's AudioTrack, or the native Oboe output ([OboeAudioOutput]) when
 * [nativeOutput] says so (asked on the playback thread for every output) and the config is one it
 * plays. Only [AudioOutputProvider.getAudioOutput] is answered here; format support, the output config
 * and its buffer size stay the AudioTrack provider's, so Media3 decides everything as before. A config
 * the native output refuses, or a stream that does not open, gets the AudioTrack.
 *
 * [onAudioOutput] hears each output's config (rate, channel mask, encoding) on the playback thread just
 * before its AudioTrack is built: the one moment a platform choice that is fixed at track creation can
 * still be made, which `playback/BitPerfectOutput` needs (issue #353). The tap's format callback, which
 * it used before, runs only after Media3 has opened the first track.
 *
 * The sink is wrapped in a [PlayoutSink], which reports to the native playout clock [playout] against
 * the [ring] the tap writes.
 */
@OptIn(UnstableApi::class)
class TapRenderersFactory(
    context: Context,
    private val sink: TeeAudioProcessor.AudioBufferSink,
    private val ring: SampleRing,
    private val playout: Long,
    private val dsp: List<AudioProcessor> = emptyList(),
    private val nativeOutput: () -> Boolean = { false },
    private val onAudioOutput: (AudioOutputProvider.OutputConfig) -> Unit = {},
) : DefaultRenderersFactory(context) {
    internal fun audioProcessorChain(): MvzAudioProcessorChain = MvzAudioProcessorChain(TeeAudioProcessor(sink), dsp = dsp)

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
                .setAudioOutputProvider(
                    // The provider DefaultAudioSink builds by default, with a look at each config first.
                    object : ForwardingAudioOutputProvider(AudioTrackAudioOutputProvider.Builder(context).build()) {
                        override fun getAudioOutput(config: AudioOutputProvider.OutputConfig): AudioOutput {
                            onAudioOutput(config)
                            val oboe = if (nativeOutput()) OboeAudioOutput.open(config) else null
                            return oboe ?: super.getAudioOutput(config)
                        }
                    },
                ).build()
        return PlayoutSink(built, playout, ring)
    }
}
