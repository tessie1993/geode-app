package dev.geode.cast

import android.app.ForegroundServiceStartNotAllowedException
import android.content.Context
import androidx.annotation.MainThread
import androidx.annotation.OptIn
import androidx.media3.cast.RemoteCastPlayer
import androidx.media3.cast.SessionAvailabilityListener
import androidx.media3.common.util.UnstableApi
import com.google.android.gms.cast.framework.CastContext
import dev.geode.R
import dev.geode.billing.ProSnapshot
import dev.geode.pro.ProFeature
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Why a cast ended early; the UI words it. */
enum class CastFailure {
    /** The stream couldn't start, froze, or the renderer died. */
    STREAM,

    /** No Wi-Fi or Ethernet network to serve the TV from (cellular or a VPN alone can't reach it). */
    NO_LAN,

    /** Android refused to start the cast's foreground service with the app in the background. */
    BACKGROUND_START,
}

/**
 * Where casting stands, for the Cast button. [Locked], [Failed] and [TrialEnded] are one-shot: they hold until
 * the UI has shown them and calls [CastController.consumed].
 */
sealed interface CastState {
    data object Idle : CastState

    /** A TV is connected and the stream is starting (the TV shows it a few seconds later). */
    data class Connecting(
        val deviceName: String?,
    ) : CastState

    data class Casting(
        val deviceName: String?,
    ) : CastState

    /** A TV was picked without Pro; the session was ended. The UI shows the paywall. */
    data object Locked : CastState

    /** The cast could not start or stopped early; the session was ended. */
    data class Failed(
        val reason: CastFailure,
    ) : CastState

    /** The trial ran out while casting; the cast stopped at the end of that song. */
    data object TrialEnded : CastState
}

/**
 * Cast to TV, as the app sees it: one per process, on the main thread.
 *
 * Google's Default Media Receiver plays a live stream the phone serves ([CastService],
 * [CastStream]); there is no receiver app of our own. The music's own player and its media session
 * stay on the phone: transport, queue and style changes all happen here, and the TV shows the
 * result a few seconds later. So this is not Media3's `CastPlayer` swapped into the session (that
 * would hand the music queue to the TV), but a [RemoteCastPlayer] that only ever loads the one
 * live item.
 *
 * Sessions start from the app's Cast button (`ui/cast/CastButton`) or from the system's output
 * switcher (`MediaTransferReceiver`); [CastOptionsProvider] builds this as soon as Cast starts, so
 * both are gated and streamed the same way. The Pro gate is checked here, when a session appears,
 * and [CastService] re-checks it while casting (a trial that runs out ends the cast after the song).
 */
@OptIn(UnstableApi::class)
class CastController private constructor(
    context: Context,
) {
    private val app = context.applicationContext

    private val remote: RemoteCastPlayer =
        RemoteCastPlayer
            .Builder(app)
            .setMediaItemConverter(LiveStreamConverter(app.getString(R.string.cast_stream_title)))
            .build()

    init {
        remote.setSessionAvailabilityListener(
            object : SessionAvailabilityListener {
                override fun onCastSessionAvailable() = onSessionStarted()

                override fun onCastSessionUnavailable() = onSessionEnded()
            },
        )
        if (remote.isCastSessionAvailable) onSessionStarted()
    }

    /** Ends casting: the TV stops and the phone's sound comes back. */
    @MainThread
    fun stop() {
        endSession()
        if (mutableState.value !is CastState.Connecting) mutableState.value = CastState.Idle
    }

    private fun onSessionStarted() {
        if (!ProSnapshot.gate(app).allows(ProFeature.CAST)) {
            mutableState.value = CastState.Locked
            endSession()
            return
        }
        mutableState.value = CastState.Connecting(deviceName())
        val refused = CastService.start(app) ?: return
        onStreamFailed(if (refused is ForegroundServiceStartNotAllowedException) CastFailure.BACKGROUND_START else CastFailure.STREAM)
    }

    private fun onSessionEnded() {
        CastService.stop(app)
        val s = mutableState.value
        if (s !is CastState.Locked && s !is CastState.Failed && s !is CastState.TrialEnded) mutableState.value = CastState.Idle
    }

    /** [CastService] calls this once the stream has enough segments for the receiver to start on. */
    internal fun onStreamReady(playlistUrl: String) {
        if (!remote.isCastSessionAvailable) return
        remote.setMediaItem(LiveStreamConverter.liveItem(playlistUrl))
        remote.prepare()
        remote.play()
        mutableState.value = CastState.Casting(deviceName())
    }

    internal fun onStreamFailed(reason: CastFailure) {
        mutableState.value = CastState.Failed(reason)
        endSession()
    }

    /** [CastService]: the trial ran out and the song it was casting has ended. */
    internal fun onTrialEnded() {
        mutableState.value = CastState.TrialEnded
        endSession()
    }

    private fun endSession() {
        CastContext.getSharedInstance()?.sessionManager?.endCurrentSession(true)
    }

    private fun deviceName(): String? =
        CastContext
            .getSharedInstance()
            ?.sessionManager
            ?.currentCastSession
            ?.castDevice
            ?.friendlyName

    companion object {
        @Volatile
        private var instance: CastController? = null

        private val mutableState = MutableStateFlow<CastState>(CastState.Idle)

        /** Process-wide, so the UI can follow it without building Cast. */
        val state: StateFlow<CastState> = mutableState.asStateFlow()

        /** The UI has shown a one-shot state ([CastState.Locked], [CastState.Failed], [CastState.TrialEnded]). */
        @MainThread
        fun consumed(shown: CastState) {
            mutableState.compareAndSet(shown, CastState.Idle)
        }

        /** The process's controller; builds it (and Cast) on first use. Main thread. */
        @MainThread
        fun get(context: Context): CastController = instance ?: CastController(context).also { instance = it }
    }
}
