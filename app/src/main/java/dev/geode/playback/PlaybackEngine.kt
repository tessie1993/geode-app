package dev.geode.playback

import android.content.Context
import androidx.annotation.OptIn
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.analysis.PlaybackMath
import dev.geode.audio.AudioFxController
import dev.geode.audio.AudioFxPresets
import dev.geode.audio.PcmRingBuffer
import dev.geode.audio.TapRenderersFactory
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.HistoryStore
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.SessionStore
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

    private val prefsFiles = GeodePrefsFiles(context)

    internal val dsp = NativeDspProcessor()

    /**
     * Settings › Playback › Bit-perfect USB output; told every AudioTrack's format before it opens (the
     * player's output provider), and tells the native DSP whether to step aside for it.
     */
    private val bitPerfect = BitPerfectOutput(context) { dsp.bitPerfect = it }

    internal val tap =
        PcmTap(captureSink) { format ->
            val hook = onAudioFormat
            if (hook != null) {
                hook(format.sampleRateHz, format.channelCount, format.encoding)
            } else {
                sampleRateHz = format.sampleRateHz
            }
        }.apply {
            boundaryListener =
                TapBoundaryListener { ended, endedFrames, begun ->
                    sampleRing.beginEpoch()
                    clockDriver.onTapBoundary(ended, endedFrames, begun)
                }
        }

    /**
     * The one player: ExoPlayer, with the tap first in its processor chain ([TapRenderersFactory]).
     * WAKE_MODE_LOCAL takes a partial wake lock while playback is active, so the CPU cannot doze
     * mid-track with the screen off. Not the WIFI variant: nothing streams.
     */
    val player: ExoPlayer =
        ExoPlayer
            .Builder(
                context,
                TapRenderersFactory(context, tap, clockDriver, dsp = listOf(dsp)) { out ->
                    bitPerfect.onOutput(out.sampleRate, out.channelMask, out.encoding)
                },
            ).setMediaSourceFactory(
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

    val audioFx = AudioFxController(prefsFiles.audioFx, AudioFxPresets.all(context), dsp)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    val replayGain = ReplayGain(context.contentResolver, scope) { audioFx.setGainDb(it) }

    /** The UI's pause/skip and sleep fades, multiplied by [crossfade]'s gain into the one player volume. */
    private var uiVolume = 1f

    private var crossfadeGain = 1f

    val sleepTimer = SleepTimer(player, scope, ::setUiVolume)

    private val crossfade =
        TrackCrossfade(player, scope) { gain ->
            crossfadeGain = gain
            pushVolume()
        }

    private val headphones = HeadphoneAutoplay(context, player)

    private val autoDirector = AutoDirectorFeed(context.applicationContext, player, scope)

    /**
     * The process's one play history. The UI reads it (Recent, play counts) and adds listen time to it;
     * [recorder] records the plays. One instance, because each [HistoryStore] holds its own copy in
     * memory and writes the whole file: two writers would undo each other's plays.
     */
    val history = HistoryStore(context)

    private val sessionStore = SessionStore(context)

    /** Saves the resumption queue and records plays whoever holds the player, UI or service alone (#371). */
    private val recorder =
        PlayRecorder(PlayRecorder.sourceOf(player), history, scope) { saved ->
            runCatching { sessionWriter.execute { sessionStore.save(saved) } }
        }

    /** Bumps once per recorded play; the UI's history tick follows it. */
    val playsRecorded = recorder.plays

    /**
     * The rate of whichever source writes [sampleRing] right now: the tap's format when nothing else
     * claims it, otherwise what the UI session's capture controller sets (mic, other apps' audio, or
     * back to the player). Written from the playback and capture threads, read on every GL thread.
     */
    @Volatile
    var sampleRateHz: Int = DEFAULT_SAMPLE_RATE_HZ

    /** What every surface's sound engine reads; the rate follows whichever source owns the ring. */
    val soundInput = SoundInput(sampleRing) { sampleRateHz }

    init {
        player.addListener(replayGain)
        player.addListener(autoDirector)
        player.addListener(crossfade)
        player.addListener(recorder)
        dev.geode.audio.AudioBus.soundInput = soundInput
        // The saved options go on here, not only when the UI attaches: a session the service starts
        // alone (a headset button, Android Auto or System UI resuming playback) has no PlayerSession,
        // and without this it would ignore pause-on-unplug, ReplayGain, shuffle and repeat until the
        // app was opened. The UI applies the same prefs again when it attaches, which is harmless.
        val saved = PlayerPrefsStore(prefsFiles.player).load()
        if (BehaviourStore(prefsFiles.general).load().rememberShuffleRepeat) {
            player.shuffleModeEnabled = saved.shuffle
            player.repeatMode = saved.repeatMode
        }
        applyPrefs(saved)
    }

    /** The UI's own fades (pause, resume, skip, sleep) set this; [crossfade] multiplies in on top. */
    fun setUiVolume(volume: Float) {
        uiVolume = volume
        pushVolume()
    }

    /**
     * Cast to TV plays the sound on the TV only: the phone keeps decoding, since the tap feeds the
     * visuals, but stays silent. `cast/CastService` sets and clears it; the fades keep their values
     * underneath, so clearing it brings back exactly the volume they would have now.
     */
    var castMuted: Boolean = false
        set(value) {
            field = value
            pushVolume()
        }

    private fun pushVolume() {
        player.volume = if (castMuted) 0f else (uiVolume * crossfadeGain).coerceIn(0f, 1f)
    }

    /** Everything in [PlayerPrefs] that the player, ReplayGain or the USB route carry out. */
    fun applyPrefs(p: PlayerPrefs) {
        player.playbackParameters = PlaybackParameters(p.speed, PlaybackMath.semitonesToRatio(p.pitchSemitones))
        player.skipSilenceEnabled = p.skipSilence
        player.setHandleAudioBecomingNoisy(p.pauseOnNoisy)
        replayGain.configure(p.replayGainMode, p.replayGainPreampDb, p.replayGainClipGuard)
        bitPerfect.setEnabled(p.bitPerfect, player)
        crossfade.crossfadeMs = p.crossfadeMs
    }

    val playbackWanted: Boolean
        get() =
            player.playWhenReady &&
                player.playbackState != Player.STATE_IDLE &&
                player.playbackState != Player.STATE_ENDED

    internal fun release() {
        if (dev.geode.audio.AudioBus.soundInput === soundInput) dev.geode.audio.AudioBus.soundInput = null
        recorder.release()
        scope.cancel()
        onAudioFormat = null
        audioFx.release()
        player.removeListener(replayGain)
        player.removeListener(autoDirector)
        player.removeListener(crossfade)
        player.removeListener(recorder)
        headphones.release()
        autoDirector.release()
        bitPerfect.setEnabled(false, player)
        player.release()
        dsp.release()
    }

    private companion object {
        const val DEFAULT_SAMPLE_RATE_HZ = 48_000

        /** One file, written in order off the main thread; a daemon, like the history writer. */
        val sessionWriter: java.util.concurrent.ExecutorService =
            java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                Thread(r, "geode-session").apply { isDaemon = true }
            }
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
