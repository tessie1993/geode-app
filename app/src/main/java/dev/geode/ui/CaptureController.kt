package dev.geode.ui

import android.app.Application
import dev.geode.audio.CaptureFailure
import dev.geode.audio.MicCapture
import dev.geode.audio.NowPlayingBridge
import dev.geode.audio.PlaybackCapture
import dev.geode.audio.PlaybackCaptureService
import dev.geode.audio.playbackCaptureSupported
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

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

        fun resetAnalysis()

        fun setAnalysisRate(rateHz: Int)

        fun setMicReactivePref(on: Boolean)
    }

    private val micCapture = MicCapture(application, capture)

    private val _micState = MutableStateFlow(MicState())

    val micState: StateFlow<MicState> = _micState

    private val playbackCapture = PlaybackCapture(capture)

    private val nowPlayingBridge = NowPlayingBridge(application)

    private val _externalAudio = MutableStateFlow(ExternalAudioState())

    val externalAudio: StateFlow<ExternalAudioState> = _externalAudio

    val micActive: Boolean get() = micCapture.active

    @Volatile
    private var tapSampleRateHz: Int = 0

    val audioFormatHook: (sampleRateHz: Int, channelCount: Int, encoding: Int) -> Unit =
        { rate, _, _ ->
            tapSampleRateHz = rate
            if (!externalAudioOwnsAnalyzer()) host.setAnalysisRate(rate)
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
        scope.launch {
            dev.geode.audio.MediaProjectionHolder.projection
                .collect { projection ->
                    if (projection != null) {
                        startPlaybackCapture(projection)
                    } else if (_externalAudio.value.active) {
                        playbackCapture.stop()
                        host.resetAnalysis()
                        host.setAnalysisRate(playbackRateFallback())
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

    private fun externalAudioOwnsAnalyzer(): Boolean = micCapture.active || playbackCapture.active

    fun setMicEnabled(enabled: Boolean): MicCapture.Failure? {
        if (!enabled) {
            micCapture.stop()
            host.resetAnalysis()
            host.setAnalysisRate(playbackRateFallback())
            _micState.value = MicState(active = false)
            host.setMicReactivePref(false)
            return null
        }
        if (micCapture.active) return null
        if (playbackCapture.active || _externalAudio.value.active) stopExternalAudio()
        host.pausePlayback()
        val failure = micCapture.start { rate -> host.setAnalysisRate(rate) }
        if (failure != null) {
            _micState.value = MicState(active = false, failure = failure)
            return failure
        }
        host.resetAnalysis()
        _micState.value = MicState(active = true)
        host.setMicReactivePref(true)
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
        if (micCapture.active) setMicEnabled(false)
        val failure = playbackCapture.start(projection) { rate -> host.setAnalysisRate(rate) }
        host.resetAnalysis()
        if (failure != null) {
            host.setAnalysisRate(playbackRateFallback())
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
        host.resetAnalysis()
        host.setAnalysisRate(playbackRateFallback())
        PlaybackCaptureService.stop(application)
        _externalAudio.update {
            it.copy(active = false, awaitingConsent = false, refusedByApp = false)
        }
    }

    fun notificationAccessIntent(): android.content.Intent = nowPlayingBridge.settingsIntent()

    fun refreshExternalAudio() {
        val state = _externalAudio.value
        val access = nowPlayingBridge.hasAccess()
        val now = if (access) nowPlayingBridge.current() else null
        val refused = playbackCapture.active && playbackCapture.blockedLikely && (now?.playing ?: false)
        val next =
            state.copy(
                active = playbackCapture.active,
                nowPlaying = now,
                hasSessionAccess = access,
                refusedByApp = refused,
            )
        if (next != state) _externalAudio.value = next
    }

    fun refreshMicState() {
        if (!_micState.value.active || micCapture.active) return
        host.resetAnalysis()
        host.setAnalysisRate(playbackRateFallback())
        _micState.value = MicState(active = false, failure = MicCapture.Failure.UNAVAILABLE)
        host.setMicReactivePref(false)
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
