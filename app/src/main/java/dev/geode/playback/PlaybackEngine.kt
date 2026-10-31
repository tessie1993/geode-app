package dev.geode.playback

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.BuildConfig
import dev.geode.analysis.PlaybackMath
import dev.geode.audio.AudioFxController
import dev.geode.audio.AudioFxPresets
import dev.geode.audio.PcmRingBuffer
import dev.geode.audio.TapRenderersFactory
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsStore
import dev.geode.engine.audio.AudioPresentationClock
import dev.geode.engine.audio.PcmSink
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import dev.geode.engine.audioandroid.PcmTap
import dev.geode.engine.audioandroid.SinkClockDriver
import dev.geode.engine.audioandroid.TapBoundaryListener
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

@OptIn(UnstableApi::class)
class PlaybackSession internal constructor(
    context: Context,
) {
    val ring = PcmRingBuffer()

    internal val sampleRing = SampleRing(capacityFrames = 1 shl 16, channelCount = 2)

    @Volatile
    var onAudioFormat: ((sampleRateHz: Int, channelCount: Int, encoding: Int) -> Unit)? = null

    internal val presentationClock = AudioPresentationClock()

    internal val clockDriver = SinkClockDriver(presentationClock)

    internal val captureSink =
        PcmSink { samples, frames, channels ->
            ring.writeInterleaved(samples, frames, channels)
            sampleRing.write(samples, frames, channels)
        }

    internal val tap =
        PcmTap(captureSink) { format ->
            val hook = onAudioFormat
            if (hook != null) {
                hook(format.sampleRateHz, format.channelCount, format.encoding)
            } else {
                analysis.sampleRateHz = format.sampleRateHz
            }
        }.apply {
            boundaryListener =
                TapBoundaryListener { ended, endedFrames, begun ->
                    sampleRing.beginEpoch()
                    clockDriver.onTapBoundary(ended, endedFrames, begun)
                }
        }

    internal val dsp = NativeDspProcessor()

    private val prefsFiles = GeodePrefsFiles(context)

    /**
     * Read once here: the engine choice is fixed for the life of the session. Release builds always
     * use ExoPlayer: the native engine still lacks audio focus and becoming-noisy handling, and its
     * index-keyed pre-roll plays the wrong track after a queue edit (docs/BUG_HUNT_2026-10.md).
     */
    val nativeEngine: Boolean = BuildConfig.DEBUG && PlayerPrefsStore(prefsFiles.player).load().nativeEngine

    // WAKE_MODE_LOCAL takes a partial wake lock while playback is active, so the CPU
    // cannot doze mid-track with the screen off. Not the WIFI variant: nothing streams.
    val exoPlayer: ExoPlayer? =
        if (nativeEngine) {
            null
        } else {
            ExoPlayer
                .Builder(context, TapRenderersFactory(context, tap, clockDriver, dsp = listOf(dsp)))
                .setMediaSourceFactory(
                    androidx.media3.exoplayer.source.DefaultMediaSourceFactory(
                        context,
                        androidx.media3.extractor.ExtractorsFactory {
                            androidx.media3.extractor
                                .DefaultExtractorsFactory()
                                .createExtractors() +
                                dev.geode.audio.AiffExtractor()
                        },
                    ),
                ).setAudioAttributes(
                    AudioAttributes
                        .Builder()
                        .setUsage(C.USAGE_MEDIA)
                        .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                        .build(),
                    true,
                ).setWakeMode(C.WAKE_MODE_LOCAL)
                .build()
        }

    val player: Player = exoPlayer ?: NativePlayer(context, NativeTapPump(tap, clockDriver), NativePlayerDsp(dsp))

    val audioFx = AudioFxController(prefsFiles.audioFx, AudioFxPresets.all(context), dsp)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    val replayGain = ReplayGain(context.contentResolver, scope) { audioFx.setGainDb(it) }

    val sleepTimer = SleepTimer(player, scope)

    val analysis = dev.geode.analysis.AnalysisEngine(sampleRing)

    /** What every surface's sound engine reads; the rate follows whichever source owns the ring. */
    val soundInput = SoundInput(sampleRing) { analysis.sampleRateHz }

    private val interestHook: () -> Unit = { syncAnalysis() }

    init {
        player.addListener(replayGain)
        // A session the service starts on its own (a headset's play button after process death,
        // Android Auto) never builds the UI's PlayerSession, which is what used to apply these: it
        // played with Media3 defaults, so unplugging headphones moved the music to the speaker.
        PlayerPrefsStore(prefsFiles.player).load().coerced().let { prefs ->
            player.shuffleModeEnabled = prefs.shuffle
            player.repeatMode = prefs.repeatMode
            applyPlayerPrefs(player, replayGain, prefs)
        }
        dev.geode.audio.AudioBus.onInterestChanged = interestHook
        dev.geode.audio.AudioBus.soundInput = soundInput
        syncAnalysis()
        scope.launch {
            analysis.features.collect {
                dev.geode.audio.AudioBus
                    .publish(it)
            }
        }
    }

    private fun syncAnalysis() {
        if (dev.geode.audio.AudioBus.hasConsumers) analysis.start(scope) else analysis.stop()
    }

    val playbackWanted: Boolean
        get() =
            player.playWhenReady &&
                player.playbackState != Player.STATE_IDLE &&
                player.playbackState != Player.STATE_ENDED

    internal fun release() {
        analysis.close()
        if (dev.geode.audio.AudioBus.onInterestChanged === interestHook) {
            dev.geode.audio.AudioBus.onInterestChanged = null
        }
        if (dev.geode.audio.AudioBus.soundInput === soundInput) dev.geode.audio.AudioBus.soundInput = null
        scope.cancel()
        onAudioFormat = null
        audioFx.release()
        player.removeListener(replayGain)
        player.release()
        dsp.release()
    }
}

object PlaybackEngine {
    private var app: Context? = null
    private var session: PlaybackSession? = null
    private var uiHolds = 0
    private var serviceHolds = 0

    private fun rebindTo(context: Context): Context {
        val current = context.applicationContext
        if (app !== current) {
            // Dropping the reference is not enough: the old session owns an ExoPlayer, the audio
            // effect chain, the PCM tap and a coroutine scope, none of which the GC reclaims.
            session?.release()
            session = null
            uiHolds = 0
            serviceHolds = 0
            app = current
        }
        return current
    }

    @Synchronized
    private fun sessionFor(context: Context): PlaybackSession {
        val current = rebindTo(context)
        return session ?: PlaybackSession(current).also { session = it }
    }

    @Synchronized
    fun acquireForUi(context: Context): PlaybackSession = sessionFor(context).also { uiHolds++ }

    @Synchronized
    fun releaseUi() {
        if (uiHolds > 0) uiHolds--
        releaseIfUnused()
    }

    @Synchronized
    fun acquireForService(context: Context): PlaybackSession = sessionFor(context).also { serviceHolds++ }

    @Synchronized
    fun releaseService() {
        if (serviceHolds > 0) serviceHolds--
        releaseIfUnused()
    }

    private fun releaseIfUnused() {
        if (uiHolds > 0 || serviceHolds > 0) return
        session?.release()
        session = null
        app = null
    }
}

/**
 * The prefs that live on the player itself rather than in the UI: speed and pitch, becoming-noisy,
 * skip-silence, crossfade and gapless, ReplayGain. One function so the session's own start-up and
 * Settings apply exactly the same set.
 */
@OptIn(UnstableApi::class)
fun applyPlayerPrefs(
    player: Player,
    replayGain: ReplayGain,
    p: PlayerPrefs,
) {
    player.playbackParameters = PlaybackParameters(p.speed, PlaybackMath.semitonesToRatio(p.pitchSemitones))
    (player as? ExoPlayer)?.let {
        it.skipSilenceEnabled = p.skipSilence
        it.setHandleAudioBecomingNoisy(p.pauseOnNoisy)
    }
    (player as? NativePlayer)?.applyPrefs(p.crossfadeMs, p.crossfadeCurve, p.gapless)
    replayGain.configure(p.replayGainMode, p.replayGainPreampDb, p.replayGainClipGuard)
}
