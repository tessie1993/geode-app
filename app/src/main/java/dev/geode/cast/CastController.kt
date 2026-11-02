package dev.geode.cast

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

/** Where casting stands, for the Cast button and the remote screen. */
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

    /** The stream could not start (no Wi-Fi address, the encoder refused). The session was ended. */
    data object Failed : CastState
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
 * Sessions start from the app's Cast button (`ui/cast/CastButton`), which builds this on first
 * composition; the app declares no MediaTransferReceiver, so the system's output switcher doesn't
 * hand it sessions. The Pro gate is still checked here, when a session appears, not only on the
 * button, and [CastService] re-checks it while casting.
 */
@OptIn(UnstableApi::class)
class CastController private constructor(
    context: Context,
) {
    private val app = context.applicationContext
    private val mutableState = MutableStateFlow<CastState>(CastState.Idle)
    val state: StateFlow<CastState> = mutableState.asStateFlow()

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
        if (!CastService.start(app)) onStreamFailed()
    }

    private fun onSessionEnded() {
        CastService.stop(app)
        if (mutableState.value !is CastState.Locked && mutableState.value !is CastState.Failed) {
            mutableState.value = CastState.Idle
        }
    }

    /** [CastService] calls this once the stream has enough segments for the receiver to start on. */
    internal fun onStreamReady(playlistUrl: String) {
        if (!remote.isCastSessionAvailable) return
        remote.setMediaItem(LiveStreamConverter.liveItem(playlistUrl))
        remote.prepare()
        remote.play()
        mutableState.value = CastState.Casting(deviceName())
    }

    internal fun onStreamFailed() {
        mutableState.value = CastState.Failed
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

        /** The process's controller; builds it (and Cast) on first use. Main thread. */
        @MainThread
        fun get(context: Context): CastController = instance ?: CastController(context).also { instance = it }
    }
}
