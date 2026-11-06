package dev.geode.ui

import android.app.Application
import dev.geode.audio.CaptureFailure
import dev.geode.audio.MicCapture
import dev.geode.audio.NowPlayingBridge
import dev.geode.audio.PlaybackCapture
import dev.geode.audio.PlaybackCaptureService
import dev.geode.audio.playbackCaptureSupported
import dev.geode.engine.bridge.GeodeNative
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class MicState(
    val active: Boolean = false,
    val failure: MicCapture.Failure? = null,
)

data class ExternalAudioState(
    val supported: Boolean = playbackCaptureSupported,
    val active: Boolean = false,
    val awaitingConsent: Boolean = false,
    val failure: CaptureFailure? = null,
    val nowPlaying: NowPlayingBridge.External? = null,
    val hasSessionAccess: Boolean = false,
    val refusedByApp: Boolean = false,
) {
    val refusingApp: String? get() = if (refusedByApp) nowPlaying?.appLabel else null
}

internal class CaptureController(
    private val application: Application,
    private val scope: CoroutineScope,
    capture: dev.geode.engine.audio.PcmSink,
    private val host: Host,
) {
    interface Host {
        fun pausePlayback()

        /** The ring changed hands: a new epoch, and whether the player (not the mic or a capture) now writes it. */
        fun resetSound(playerOwnsRing: Boolean)

        fun setSoundRate(rateHz: Int)
    }

    private val micCapture = MicCapture(application, capture)

    private val _micState = MutableStateFlow(MicState())

    val micState: StateFlow<MicState> = _micState

    private val playbackCapture = PlaybackCapture(capture)

    private val nowPlayingBridge = NowPlayingBridge(application)

    private val _externalAudio = MutableStateFlow(ExternalAudioState())

    val externalAudio: StateFlow<ExternalAudioState> = _externalAudio

    /**
     * Live input's state, kept here and stepped by the native machine (`core/audio/LiveInput`),
     * which owns every decision about it: when it turns on or off, the background pause, music
     * winning, a lost capture (#367). This class only carries out the actions it returns.
     */
    private var liveInput = GeodeNative.LIVE_INPUT_OFF

    /** On, or paused while the app is away (the native rule says why that counts as on). */
    val micActive: Boolean get() = GeodeNative.liveInputIsOn(liveInput)

    /** The app left the screen. */
    fun onBackground() {
        send(GeodeNative.LIVE_INPUT_APP_BACKGROUND)
    }

    /** The app is back on screen. */
    fun onForeground() {
        send(GeodeNative.LIVE_INPUT_APP_FOREGROUND)
    }

    /** Music (or another app's audio) is starting as the source. */
    fun onMusicStarts() {
        send(GeodeNative.LIVE_INPUT_MUSIC_STARTS)
    }

    /**
     * Steps the native machine with [event] and carries out its actions in their bit order. A
     * capture that fails to start is reported back as its own event. Returns that failure, if any.
     */
    private fun send(event: Int): MicCapture.Failure? {
        val packed = GeodeNative.liveInputStep(liveInput, event, micCapture.hasPermission())
        val previous = liveInput
        liveInput = (packed and 0xff).toInt()
        val actions = (packed ushr 8).toInt()
        // Nothing to do (the poll while off, say): leave the shown state, and any start failure on it, alone.
        if (actions == 0 && liveInput == previous) return null

        fun has(bit: Int) = (actions and bit) != 0
        val otherAppAudio = playbackCapture.active || _externalAudio.value.active
        if (has(GeodeNative.LIVE_INPUT_ACT_STOP_OTHER_APP_AUDIO) && otherAppAudio) stopExternalAudio()
        if (has(GeodeNative.LIVE_INPUT_ACT_PAUSE_MUSIC)) host.pausePlayback()
        if (has(GeodeNative.LIVE_INPUT_ACT_START_CAPTURE)) {
            val failure = micCapture.start { rate -> host.setSoundRate(rate) }
            if (failure != null) {
                send(GeodeNative.LIVE_INPUT_START_FAILED)
                _micState.value = MicState(active = false, failure = failure)
                return failure
            }
        }
        if (has(GeodeNative.LIVE_INPUT_ACT_STOP_CAPTURE)) micCapture.stop()
        if (has(GeodeNative.LIVE_INPUT_ACT_RESET_SOUND)) host.resetSound(!externalAudioOwnsRing())
        if (has(GeodeNative.LIVE_INPUT_ACT_PLAYER_RATE)) host.setSoundRate(playbackRateFallback())
        _micState.value =
            MicState(
                active = micActive,
                failure = if (liveInput == GeodeNative.LIVE_INPUT_LOST) MicCapture.Failure.UNAVAILABLE else null,
            )
        return null
    }

    @Volatile
    private var tapSampleRateHz: Int = 0

    val audioFormatHook: (sampleRateHz: Int, channelCount: Int, encoding: Int) -> Unit =
        { rate, _, _ ->
            tapSampleRateHz = rate
            if (!externalAudioOwnsRing()) host.setSoundRate(rate)
        }

    /**
     * Begins watching for a media projection.
     *
     * Deliberately not an `init` block. [MediaProjectionHolder.projection] is a process-global
     * StateFlow that outlives a [PlayerSession], so a projection can already be live when one is
     * built; collecting it on `Dispatchers.Main.immediate` then runs [startPlaybackCapture]
     * synchronously inside the constructor, which calls back into a session whose later fields
     * — `player` among them — have not been assigned yet. The owner calls this once it is fully
     * constructed instead.
     */
    fun start() {
        // Live input always starts off. It is never restored after Android kills the app (owner,
        // #367): restoring it paused whatever the person had started meanwhile from the
        // notification, a headset or the car, and a mic that switches itself on is a surprise.
        // Within one process the background pause still resumes ([onForeground]).
        scope.launch {
            dev.geode.audio.MediaProjectionHolder.projection
                .collect { projection ->
                    if (projection != null) {
                        startPlaybackCapture(projection)
                    } else if (_externalAudio.value.active) {
                        playbackCapture.stop()
                        host.resetSound(!externalAudioOwnsRing())
                        host.setSoundRate(playbackRateFallback())
                        _externalAudio.update { it.copy(active = false, refusedByApp = false) }
                    }
                }
        }
        scope.launch {
            dev.geode.audio.MediaProjectionHolder.startFailures
                .drop(1)
                .collect {
                    _externalAudio.update {
                        it.copy(awaitingConsent = false, failure = CaptureFailure.CONSENT)
                    }
                }
        }
    }

    private fun playbackRateFallback(): Int = tapSampleRateHz.takeIf { it > 0 } ?: 44100

    private fun externalAudioOwnsRing(): Boolean = micCapture.active || playbackCapture.active

    fun setMicEnabled(enabled: Boolean): MicCapture.Failure? =
        send(if (enabled) GeodeNative.LIVE_INPUT_TURN_ON else GeodeNative.LIVE_INPUT_TURN_OFF)

    fun hasMicPermission(): Boolean = micCapture.hasPermission()

    fun noteExternalAudioConsentPending() {
        _externalAudio.update { it.copy(awaitingConsent = true, failure = null) }
    }

    fun noteExternalAudioConsentDenied() {
        _externalAudio.update {
            it.copy(awaitingConsent = false, failure = CaptureFailure.CONSENT)
        }
    }

    private fun startPlaybackCapture(projection: android.media.projection.MediaProjection) {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
            _externalAudio.update {
                it.copy(awaitingConsent = false, failure = CaptureFailure.UNSUPPORTED)
            }
            return
        }
        if (!micCapture.hasPermission()) {
            _externalAudio.update {
                it.copy(awaitingConsent = false, failure = CaptureFailure.PERMISSION)
            }
            return
        }
        host.pausePlayback()
        onMusicStarts()
        val failure = playbackCapture.start(projection) { rate -> host.setSoundRate(rate) }
        host.resetSound(!externalAudioOwnsRing())
        if (failure != null) {
            host.setSoundRate(playbackRateFallback())
            PlaybackCaptureService.stop(application)
        }
        _externalAudio.update {
            it.copy(
                active = failure == null,
                awaitingConsent = false,
                failure = failure,
                refusedByApp = false,
            )
        }
    }

    fun stopExternalAudio() {
        playbackCapture.stop()
        host.resetSound(!externalAudioOwnsRing())
        host.setSoundRate(playbackRateFallback())
        PlaybackCaptureService.stop(application)
        _externalAudio.update {
            it.copy(active = false, awaitingConsent = false, refusedByApp = false)
        }
    }

    fun notificationAccessIntent(): android.content.Intent = nowPlayingBridge.settingsIntent()

    @Volatile
    private var externalAudioRefreshInFlight = false

    /**
     * Polled every 500 ms from Main. [NowPlayingBridge.hasAccess] and [NowPlayingBridge.current]
     * are synchronous binder calls (a `Settings.Secure` read and a
     * `MediaSessionManager`/`PackageManager` round trip), so they run on IO instead of blocking
     * Main. Skipped only once there is truly nothing left to learn: no capture is active or
     * pending, and notification-listener access is already known to be granted — [hasSessionAccess]
     * still needs polling while it is false, since [ExternalAudioSettings] shows its "allow
     * reading" prompt from that flag alone and expects it to flip once access is granted in
     * system settings, with no capture running.
     */
    fun refreshExternalAudio() {
        val state = _externalAudio.value
        if (!state.active && !state.awaitingConsent && state.hasSessionAccess) return
        if (externalAudioRefreshInFlight) return
        externalAudioRefreshInFlight = true
        scope.launch {
            val (access, now) =
                withContext(Dispatchers.IO) {
                    val access = nowPlayingBridge.hasAccess()
                    access to (if (access) nowPlayingBridge.current() else null)
                }
            val refused = playbackCapture.active && playbackCapture.blockedLikely && (now?.playing ?: false)
            _externalAudio.update {
                it.copy(
                    active = playbackCapture.active,
                    nowPlaying = now,
                    hasSessionAccess = access,
                    refusedByApp = refused,
                )
            }
            externalAudioRefreshInFlight = false
        }
    }

    /**
     * The session's poll (every 500 ms, background included) reports a capture that is not
     * reading; the native machine decides whether that means the mic was lost (only while on,
     * never during the background pause, #367).
     */
    fun refreshMicState() {
        if (!micCapture.active) send(GeodeNative.LIVE_INPUT_CAPTURE_STOPPED)
    }

    fun shutdown() {
        micCapture.stop()
        // Stop the pump on the pump's own state, not the UI mirror of it. After this the owning
        // scope is cancelled, so the projection collector that would otherwise notice is gone —
        // a pump left running here keeps an AudioRecord and its reader thread alive, writing into
        // a released playback session, for the rest of the process.
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
    }
}
