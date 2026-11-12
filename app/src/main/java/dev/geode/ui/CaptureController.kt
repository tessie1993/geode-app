package dev.geode.ui

import android.app.Application
import dev.geode.audio.CaptureFailure
import dev.geode.audio.MicCapture
import dev.geode.audio.NowPlayingBridge
import dev.geode.audio.PlaybackCapture
import dev.geode.audio.PlaybackCaptureService
import dev.geode.cast.CastService
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
    val active: Boolean = false,
    val awaitingConsent: Boolean = false,
    val failure: CaptureFailure? = null,
    val nowPlaying: NowPlayingBridge.External? = null,
    val hasSessionAccess: Boolean = false,
    val refusedByApp: Boolean = false,
    /** Capturing, but only silence for a while: nothing plays, or the playing app blocks capture. */
    val hearingSilence: Boolean = false,
) {
    val refusingApp: String? get() = if (refusedByApp) nowPlaying?.appLabel else null
}

internal class CaptureController(
    private val application: Application,
    private val scope: CoroutineScope,
    capture: Long,
    /** The mic's own ring, layered over the player's natively: the mic never takes [capture]. */
    private val micRing: Long,
    private val host: Host,
) {
    interface Host {
        /** Before another app's audio is captured; the mic never pauses the player. */
        fun pausePlayback()

        /**
         * The ring changes hands: a new epoch, and whether the player (not a capture of another app) writes
         * it. Called with false before a capture starts writing and with the new owner once one has
         * stopped, so the ring never has two writers.
         */
        fun resetSound(playerOwnsRing: Boolean)

        fun setMicReactivePref(on: Boolean)

        /** The persisted "mic was on" preference, read once on [start] to restore capture. */
        val micReactivePref: Boolean
    }

    private val micCapture = MicCapture(application, micRing)

    private val _micState = MutableStateFlow(MicState())

    val micState: StateFlow<MicState> = _micState

    private val playbackCapture = PlaybackCapture(capture)

    private val nowPlayingBridge = NowPlayingBridge(application)

    private val _externalAudio = MutableStateFlow(ExternalAudioState())

    val externalAudio: StateFlow<ExternalAudioState> = _externalAudio

    /** The mic was stopped because the app left the screen, and comes back with it ([onForeground]). */
    private var micPausedInBackground = false

    private var inBackground = false

    /**
     * Stops the mic. The epoch ends its stream for every reader: what waited in a renderer's mix is
     * dropped, and a paused player counts as the source again.
     */
    private fun stopMic() {
        micCapture.stop()
        GeodeNative.pcmRingBeginEpoch(micRing)
        CastService.setMicWanted(application, false)
    }

    /**
     * The app left the screen. Live input exists to drive visuals someone is looking at, and
     * without a microphone foreground service Android gives a background app silence anyway,
     * so the AudioRecord and its reader thread are released instead of reading zeros. The
     * preference and the on-screen state stay as they are for [onForeground] to resume.
     * A cast is still somebody looking: while it holds the microphone type the mic keeps going.
     */
    fun onBackground() {
        inBackground = true
        if (!micCapture.active || CastService.micHeld.value) return
        pauseMicInBackground()
    }

    private fun pauseMicInBackground() {
        stopMic()
        micPausedInBackground = true
    }

    /** The app is back on screen: resume the mic [onBackground] stopped, unless access went meanwhile. */
    fun onForeground() {
        inBackground = false
        if (!micPausedInBackground) return
        micPausedInBackground = false
        if (hasMicPermission()) setMicEnabled(true) else refreshMicState()
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
        // Restore a mic-reactive session across process death. The pref was written on every
        // mic toggle but never read back, so a restart silently dropped it. Never request the
        // permission here: if it has since been revoked, leave the pref as-is and stay silent —
        // the person can re-enable it from the mic control, which will prompt as usual.
        if (host.micReactivePref && !micCapture.active && hasMicPermission()) {
            setMicEnabled(true)
        }
        // The cast ended (or lost the microphone type) with the app away: Android would hand the mic silence.
        scope.launch {
            CastService.micHeld.collect { held ->
                if (!held && inBackground && micCapture.active) pauseMicInBackground()
            }
        }
        scope.launch {
            dev.geode.audio.MediaProjectionHolder.projection
                .collect { projection ->
                    if (projection != null) {
                        startPlaybackCapture(projection)
                    } else if (_externalAudio.value.active) {
                        playbackCapture.stop()
                        host.resetSound(!externalAudioOwnsRing())
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

    private fun externalAudioOwnsRing(): Boolean = playbackCapture.active

    /**
     * The mic as a layer over playback: it writes its own ring, so the player keeps playing and keeps its
     * ring, and the visuals hear both. Only a capture of another app still gives way to it.
     */
    fun setMicEnabled(enabled: Boolean): MicCapture.Failure? {
        if (!enabled) {
            micPausedInBackground = false
            stopMic()
            _micState.value = MicState(active = false)
            host.setMicReactivePref(false)
            return null
        }
        if (micCapture.active) return null
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
        // A new epoch before the first block: nothing an earlier run left in the ring is heard.
        GeodeNative.pcmRingBeginEpoch(micRing)
        val failure = micCapture.start()
        if (failure != null) {
            _micState.value = MicState(active = false, failure = failure)
            return failure
        }
        _micState.value = MicState(active = true)
        host.setMicReactivePref(true)
        CastService.setMicWanted(application, true)
        return null
    }

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
        if (!micCapture.hasPermission()) {
            _externalAudio.update {
                it.copy(awaitingConsent = false, failure = CaptureFailure.PERMISSION)
            }
            return
        }
        host.pausePlayback()
        if (micCapture.active) setMicEnabled(false)
        host.resetSound(playerOwnsRing = false)
        val failure = playbackCapture.start(projection)
        host.resetSound(!externalAudioOwnsRing())
        if (failure != null) PlaybackCaptureService.stop(application)
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
            val refused = playbackCapture.active && playbackCapture.quiet && (now?.playing ?: false)
            _externalAudio.update {
                it.copy(
                    active = playbackCapture.active,
                    nowPlaying = now,
                    hasSessionAccess = access,
                    refusedByApp = refused,
                    hearingSilence = playbackCapture.active && playbackCapture.quiet,
                )
            }
            externalAudioRefreshInFlight = false
        }
    }

    fun refreshMicState() {
        if (!_micState.value.active || micCapture.active) return
        GeodeNative.pcmRingBeginEpoch(micRing)
        CastService.setMicWanted(application, false)
        _micState.value = MicState(active = false, failure = MicCapture.Failure.UNAVAILABLE)
        host.setMicReactivePref(false)
    }

    fun shutdown() {
        stopMic()
        // Stop the pump on the pump's own state, not the UI mirror of it. After this the owning
        // scope is cancelled, so the projection collector that would otherwise notice is gone —
        // a pump left running here keeps an AudioRecord and its reader thread alive, writing into
        // a released playback session, for the rest of the process.
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
        // The playback session outlives this one: its tap must not stay shut out of its own ring.
        host.resetSound(playerOwnsRing = true)
    }
}
