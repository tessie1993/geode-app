package dev.geode.ui

import android.Manifest
import android.app.Application
import android.content.pm.PackageManager
import android.media.AudioManager
import androidx.core.content.ContextCompat
import dev.geode.audio.AudioBus
import dev.geode.audio.CaptureFailure
import dev.geode.audio.NowPlayingBridge
import dev.geode.audio.OutputMixCapture
import dev.geode.audio.PlaybackCapture
import dev.geode.audio.PlaybackCaptureService
import dev.geode.cast.CastService
import dev.geode.engine.bridge.IngestNative
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
    val failure: CaptureFailure? = null,
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

    /** The other app while it is the sound source and its media session answers: the transport is its, not the player's. */
    val driven: NowPlayingBridge.External? get() = nowPlaying.takeIf { active }

    /** Heard, and its session (when read) not paused: the device mix that stands in for a quiet capture hears silence too. */
    val playing: Boolean get() = active && !hearingSilence && driven?.playing != false

    /** Capturing without notification access: the other app can be heard but not named or driven. */
    val sessionUnreadable: Boolean get() = active && !hasSessionAccess
}

/**
 * Sound is driving the visual, the input Auto holds on: the app's own player, the mic, or another app's capture.
 * A capture pauses the player, so the player alone would hold Auto for as long as another app is visualized.
 */
internal fun soundPlaying(
    playerPlaying: Boolean,
    external: ExternalAudioState,
    mic: MicState,
): Boolean = playerPlaying || mic.active || external.playing

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

    private val _micState = MutableStateFlow(MicState())

    val micState: StateFlow<MicState> = _micState

    private val playbackCapture = PlaybackCapture(capture)

    private val nowPlayingBridge = NowPlayingBridge(application) { now -> _externalAudio.update { it.copy(nowPlaying = now) } }

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
        IngestNative.micStop()
        IngestNative.pcmRingBeginEpoch(micRing)
        CastService.setMicWanted(application, false)
    }

    /**
     * The app left the screen. Live input exists to drive visuals someone is looking at, and
     * without a microphone foreground service Android gives a background app silence anyway,
     * so the native input stream is closed instead of reading zeros. The
     * preference and the on-screen state stay as they are for [onForeground] to resume.
     * A cast is still somebody looking: while it holds the microphone type the mic keeps going.
     */
    fun onBackground() {
        inBackground = true
        if (!IngestNative.micActive() || CastService.micHeld.value) return
        pauseMicInBackground()
    }

    private fun pauseMicInBackground() {
        stopMic()
        micPausedInBackground = true
    }

    /** The app is back on screen: resume the mic [onBackground] stopped, unless access went meanwhile. */
    fun onForeground() {
        inBackground = false
        // Notification access is granted in system settings, so coming back is when to look again.
        notificationAccessAbsent = false
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
        if (host.micReactivePref && !IngestNative.micActive() && hasMicPermission()) {
            setMicEnabled(true)
        }
        // The cast ended (or lost the microphone type) with the app away: Android would hand the mic silence.
        scope.launch {
            CastService.micHeld.collect { held ->
                if (!held && inBackground && IngestNative.micActive()) pauseMicInBackground()
            }
        }
        scope.launch {
            dev.geode.audio.MediaProjectionHolder.projection
                .collect { projection ->
                    if (projection != null) {
                        startPlaybackCapture(projection)
                    } else if (_externalAudio.value.active) {
                        nowPlayingBridge.unwatch()
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
    fun setMicEnabled(enabled: Boolean): CaptureFailure? {
        if (!enabled) {
            micPausedInBackground = false
            stopMic()
            _micState.value = MicState(active = false)
            host.setMicReactivePref(false)
            return null
        }
        if (IngestNative.micActive()) return null
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
        // A new epoch before the first block: nothing an earlier run left in the ring is heard.
        IngestNative.pcmRingBeginEpoch(micRing)
        val failure = startMic()
        if (failure != null) {
            _micState.value = MicState(active = false, failure = failure)
            return failure
        }
        _micState.value = MicState(active = true)
        host.setMicReactivePref(true)
        CastService.setMicWanted(application, true)
        return null
    }

    /** The mic's stream is native (`core/audio/ingest/MicInput`); this hands it the ring and the device's answers. */
    private fun startMic(): CaptureFailure? {
        if (!hasMicPermission()) return CaptureFailure.PERMISSION
        val audio = application.getSystemService(AudioManager::class.java)
        val rate = audio?.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 0
        val unprocessed = audio?.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
        return if (IngestNative.micStart(rate, unprocessed)) null else CaptureFailure.UNAVAILABLE
    }

    fun hasMicPermission(): Boolean =
        ContextCompat.checkSelfPermission(application, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    fun noteExternalAudioConsentPending() {
        _externalAudio.update { it.copy(awaitingConsent = true, failure = null) }
    }

    /**
     * The phone's output mix in place of a capture that hears only digital zero: an app that refuses capture
     * (most streaming apps) is still in the mix. Coarser than the capture (see [OutputMixCapture]), so it runs
     * only while the capture is quiet, and a phone that refuses the mix is asked once per capture.
     */
    private fun hearDeviceMix(wanted: Boolean) {
        if (wanted == deviceMix.running) return
        if (!wanted || deviceMixRefused) {
            stopDeviceAudio()
            return
        }
        if (deviceMix.start(application)) {
            AudioBus.deviceInput = deviceMix.soundInput
        } else {
            deviceMixRefused = true
        }
    }

    private var deviceMixRefused = false

    /** The other app's transport, through its media session: needs the notification access [refreshExternalAudio] reports. */
    fun externalPlayPause() = control { nowPlayingBridge.playPause() }

    fun externalNext() = control { nowPlayingBridge.next() }

    fun externalPrevious() = control { nowPlayingBridge.previous() }

    fun externalSeekTo(fraction: Float) = control { nowPlayingBridge.seekTo(fraction) }

    /** The other app's playhead while its session is followed, else null. */
    fun externalPositionMs(): Long? = if (_externalAudio.value.driven != null) nowPlayingBridge.positionMs() else null

    fun openNotificationAccess(from: android.content.Context) = nowPlayingBridge.openAccessSettings(from)

    private fun control(action: () -> Unit) {
        scope.launch(Dispatchers.IO) { action() }
    }

    /** The stage goes back to the player's ring before the mix stops being written. */
    private fun stopDeviceAudio() {
        AudioBus.deviceInput = 0L
        deviceMix.stop()
    }

    fun noteExternalAudioConsentDenied() {
        _externalAudio.update {
            it.copy(awaitingConsent = false, failure = CaptureFailure.CONSENT)
        }
    }

    /** The mic permission was refused before Android's capture dialog: a different refusal, with Settings as the way back. */
    fun noteExternalAudioPermissionDenied() {
        _externalAudio.update {
            it.copy(awaitingConsent = false, failure = CaptureFailure.PERMISSION)
        }
    }

    private fun startPlaybackCapture(projection: android.media.projection.MediaProjection) {
        if (!hasMicPermission()) {
            // No capture without the permission: the service and its projection go, as when a start fails below.
            PlaybackCaptureService.stop(application)
            _externalAudio.update {
                it.copy(awaitingConsent = false, failure = CaptureFailure.PERMISSION)
            }
            return
        }
        host.pausePlayback()
        if (IngestNative.micActive()) setMicEnabled(false)
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
        followSession()
    }

    /**
     * Follows the other app's media session while a capture runs and access is granted, so its title, art and
     * state arrive as they change (callbacks, not the poll); stops following otherwise.
     */
    private fun followSession() {
        val state = _externalAudio.value
        if (state.active && state.hasSessionAccess) nowPlayingBridge.watch() else nowPlayingBridge.unwatch()
    }

    fun stopExternalAudio() {
        nowPlayingBridge.unwatch()
        stopDeviceAudio()
        deviceMixRefused = false
        playbackCapture.stop()
        host.resetSound(!externalAudioOwnsRing())
        PlaybackCaptureService.stop(application)
        _externalAudio.update {
            it.copy(active = false, awaitingConsent = false, refusedByApp = false)
        }
    }

    @Volatile
    private var externalAudioRefreshInFlight = false

    /** Notification access was checked and is not granted: [refreshExternalAudio] stops asking until [onForeground]. */
    @Volatile
    private var notificationAccessAbsent = false

    /**
     * Polled every 500 ms from Main. [NowPlayingBridge.hasAccess] is a synchronous binder call, so it
     * runs on IO instead of blocking Main; what the other app plays arrives through [NowPlayingBridge]'s
     * callbacks ([followSession]), not through this poll. Skipped only once there is truly nothing
     * left to learn: no capture is active or
     * pending, and notification-listener access is already known: granted, or absent and not looked at
     * again until the app is back on screen ([onForeground]), which is where a grant in system
     * settings is seen. [ExternalAudioSettings] shows its "allow reading" prompt from
     * [hasSessionAccess] alone and expects it to flip then.
     */
    fun refreshExternalAudio() {
        val state = _externalAudio.value
        val idle = !state.active && !state.awaitingConsent
        val accessKnown = state.hasSessionAccess || notificationAccessAbsent
        if (idle && accessKnown) return
        if (externalAudioRefreshInFlight) return
        externalAudioRefreshInFlight = true
        scope.launch {
            try {
                val access = withContext(Dispatchers.IO) { nowPlayingBridge.hasAccess() }
                notificationAccessAbsent = !access
                // Read once: the capture thread can end between two reads of the native state.
                val capturing = playbackCapture.active
                // A capture that ended on its own (an AudioRecord error, the audio server restarting) must hand
                // the ring back as a stop does; mirrored as merely inactive, the player stays shut out of its ring.
                if (_externalAudio.value.active && !capturing) {
                    stopExternalAudio()
                    _externalAudio.update { it.copy(hasSessionAccess = access) }
                    return@launch
                }
                val quiet = capturing && playbackCapture.quiet
                hearDeviceMix(quiet)
                // On the mix the app is heard after all, so neither notice applies.
                val unheard = quiet && !deviceMix.running
                _externalAudio.update {
                    it.copy(
                        active = capturing,
                        hasSessionAccess = access,
                        refusedByApp = unheard && (it.nowPlaying?.playing ?: false),
                        hearingSilence = unheard,
                    )
                }
                followSession()
            } finally {
                externalAudioRefreshInFlight = false
            }
        }
    }

    /** A mic that stopped on its own is reported and forgotten; one [onBackground] paused is not dead, it comes back with the app. */
    fun refreshMicState() {
        if (micPausedInBackground || !_micState.value.active || IngestNative.micActive()) return
        IngestNative.pcmRingBeginEpoch(micRing)
        CastService.setMicWanted(application, false)
        _micState.value = MicState(active = false, failure = CaptureFailure.UNAVAILABLE)
        host.setMicReactivePref(false)
    }

    private companion object {
        /** One for the process: its ring is read by the stage's drawing thread, so it is never freed. */
        val deviceMix = OutputMixCapture()
    }

    fun shutdown() {
        nowPlayingBridge.unwatch()
        stopMic()
        // Stop the pump on the pump's own state, not the UI mirror of it. After this the owning
        // scope is cancelled, so the projection collector that would otherwise notice is gone —
        // a pump left running here keeps an AudioRecord and its reader thread alive, writing into
        // a released playback session, for the rest of the process.
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
        stopDeviceAudio()
        // The playback session outlives this one: its tap must not stay shut out of its own ring.
        host.resetSound(playerOwnsRing = true)
    }
}
