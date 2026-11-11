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
import dev.geode.audio.TapRenderersFactory
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.HistoryStore
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.SessionStore
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audioandroid.PcmTap
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

@OptIn(UnstableApi::class)
class PlaybackSession internal constructor(
    context: Context,
) {
    /**
     * Where the tap or a capture writes, and what every surface's sound engine reads: the one native
     * ring every renderer hears, MilkDrop's raw PCM included. Each block carries its source's rate. The
     * ring is freed once nothing holds this object, so a surface still reading it after [release] is safe.
     */
    val sampleRing = SampleRing(capacityFrames = 1 shl 16, channelCount = 2)

    private val prefsFiles = GeodePrefsFiles(context)

    internal val dsp = NativeDspProcessor()

    /**
     * Settings › Playback › Bit-perfect USB output; told every AudioTrack's format before it opens (the
     * player's output provider), and tells the native DSP whether to step aside for it.
     */
    private val bitPerfect = BitPerfectOutput(context) { dsp.bitPerfect = it }

    internal val tap = PcmTap(sampleRing)

    /**
     * The mic's own ring, layered over [sampleRing] natively (`core/audio/ingest/LayerMix`): a renderer
     * fed [sampleRing] hears the music and the room mixed, and the room alone while nothing plays. The
     * mic never writes the player's ring, so turning it on or off leaves playback alone.
     */
    val micRing =
        SampleRing(capacityFrames = 1 shl 15, channelCount = 1).also {
            GeodeNative.pcmRingSetLayer(sampleRing.handle, it.handle)
        }

    /**
     * Settings › Playback › Native audio output, as it applies now: on, and bit-perfect USB off (that
     * path is the platform track's: the mixer attributes are matched against an AudioTrack's format).
     * Read on the playback thread each time the sink builds an output.
     */
    @Volatile
    private var nativeOutput = false

    /**
     * The one player: ExoPlayer, with the tap first in its processor chain ([TapRenderersFactory]).
     * WAKE_MODE_LOCAL takes a partial wake lock while playback is active, so the CPU cannot doze
     * mid-track with the screen off. Not the WIFI variant: nothing streams.
     */
    val player: ExoPlayer =
        ExoPlayer
            .Builder(
                context,
                TapRenderersFactory(context, tap, dsp = listOf(dsp), nativeOutput = { nativeOutput }) { out ->
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

    // Every gain the playback applies (ReplayGain, the UI fades, the sleep fade, the crossfade) is a native
    // ramp on the samples, in the DSP after the tap (`core/audio/dsp/GainStage`, #483). These only tell it
    // targets, lengths and which tracks are which; `player.volume` is left to Cast's mute.
    val replayGain = ReplayGain(context.contentResolver, scope, dsp)

    val sleepTimer = SleepTimer(player, scope) { remainingMs -> GeodeNative.dspSleepFade(dsp.nativeHandle(), remainingMs) }

    private val crossfade = TrackCrossfade(player, dsp)

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

    /** Skips or stops on a track that will not play, with or without the UI; the UI reads what it did. */
    val errors = PlaybackErrorRecovery(player)

    init {
        // First, so the UI's own listener, added later, finds the failure already handled.
        player.addListener(errors)
        player.addListener(replayGain)
        player.addListener(autoDirector)
        player.addListener(crossfade)
        player.addListener(recorder)
        dev.geode.audio.AudioBus.soundInput = sampleRing
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

    /**
     * The UI fade (pause, resume, skip): native ramps to full level or to silence over [ms]. The ramp runs
     * on the samples, which are processed up to a sink buffer ahead of the speaker, so a caller that acts
     * once the fade is heard (a pause) waits [ms] plus [SINK_LAG_MS].
     */
    fun rampUiFade(
        audible: Boolean,
        ms: Int,
    ) = GeodeNative.dspRampTo(dsp.nativeHandle(), if (audible) 0f else Float.NEGATIVE_INFINITY, ms)

    /** Everything in [PlayerPrefs] that the player, ReplayGain or the USB route carry out. */
    fun applyPrefs(p: PlayerPrefs) {
        player.playbackParameters = PlaybackParameters(p.speed, PlaybackMath.semitonesToRatio(p.pitchSemitones))
        player.skipSilenceEnabled = p.skipSilence
        player.setHandleAudioBecomingNoisy(p.pauseOnNoisy)
        replayGain.configure(p.replayGainMode, p.replayGainPreampDb, p.replayGainClipGuard)
        GeodeNative.dspSetSpeed(dsp.nativeHandle(), p.speed)
        val oboe = p.nativeOutput && !(p.bitPerfect && BitPerfectOutput.supported)
        if (oboe != nativeOutput) {
            nativeOutput = oboe
            // The output already open was built under the old choice. A playing item is seeked to where
            // it stands, as for bit-perfect: Media3 rebuilds the output there, so the switch applies now.
            if (player.playbackState != Player.STATE_IDLE && player.currentMediaItem != null) {
                bestEffort(TAG, "reopen the audio output") { player.seekTo(player.currentPosition) }
            }
        }
        bitPerfect.setEnabled(p.bitPerfect, player)
        crossfade.crossfadeMs = p.crossfadeMs
    }

    val playbackWanted: Boolean
        get() =
            player.playWhenReady &&
                player.playbackState != Player.STATE_IDLE &&
                player.playbackState != Player.STATE_ENDED

    internal fun release() {
        if (dev.geode.audio.AudioBus.soundInput === sampleRing) dev.geode.audio.AudioBus.soundInput = null
        recorder.release()
        scope.cancel()
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

    companion object {
        /**
         * How long a gain ramp that ended on the samples takes to reach the speaker: DefaultAudioSink's PCM
         * buffer, 250 ms on the usual phone (its minimum). Until the playout clock (#482) says which sample
         * the speaker is on, a pause after a fade-out waits this long so it never cuts the fade short.
         */
        const val SINK_LAG_MS = 250L

        private const val TAG = "PlaybackSession"

        /** One file, written in order off the main thread; a daemon, like the history writer. */
        private val sessionWriter: java.util.concurrent.ExecutorService =
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
