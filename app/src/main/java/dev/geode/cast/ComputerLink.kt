package dev.geode.cast

import android.app.ForegroundServiceStartNotAllowedException
import android.content.Context
import android.os.SystemClock
import androidx.annotation.MainThread
import dev.geode.billing.ProSnapshot
import dev.geode.engine.bridge.GeodeNative
import dev.geode.pro.ProFeature
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Where "Show on a computer" stands, for its sheet. [Ended] holds until the UI has shown it ([ComputerLink.shown]). */
sealed interface LinkState {
    data object Idle : LinkState

    /** The stream is starting; the link is not served yet. */
    data object Starting : LinkState

    /**
     * A browser on the same Wi-Fi can open [url] now, the whole link with its token, which the sheet shows only as a
     * QR code; [typedUrl] is the short link a computer types instead, with no token, whose page asks for the code
     * the sheet shows ([ComputerLink.pairCode]). Both write the host as one number instead of the dotted address
     * (native, `geode_link_share_url`, `geode_link_pair_url`); [label] is the link's short name for the sheet.
     */
    data class Live(
        val url: String,
        val label: String = "",
        val typedUrl: String = "",
    ) : LinkState

    /** The share stopped by itself; [reason] is a GEODE_LINK_REASON_* the UI words. */
    data class Ended(
        val reason: Int,
    ) : LinkState
}

/**
 * "Show on a computer" (owner: "The connected display. Needs cast to pc option"): the Cast stream, without a
 * Chromecast, served to a browser on the same Wi-Fi through a link. Pro, like Cast to TV ([ProFeature.CAST]).
 *
 * The same pipeline as a TV cast ([CastService], [CastStream], [LiveLink]), started without a Cast
 * session; with a TV casting too they share the one stream. The sound plays in the browser and the phone goes
 * silent, as for a TV: the stream runs a few seconds behind the phone, so both at once would echo.
 *
 * What happens on each event is native (core/viz/frame/LinkShare, `geode_link_step`): Start without Pro opens
 * the paywall, without Wi-Fi ends at once, otherwise starts the stream; a failure, a network change or the end
 * of a trial ends it with a reason. This object only keeps that state for the UI and carries out the actions.
 * Process-wide, on the main thread, like [CastController.state].
 */
object ComputerLink {
    private val machine = IntArray(GeodeNative.LINK_STATE)
    private var url: String? = null
    private var label: String = ""
    private var typedUrl: String = ""
    private val mutableState = MutableStateFlow<LinkState>(LinkState.Idle)

    val state: StateFlow<LinkState> = mutableState.asStateFlow()

    /** Starts sharing; returns true when the caller should open the paywall instead. */
    @MainThread
    fun start(context: Context): Boolean {
        val app = context.applicationContext
        var facts = 0
        if (ProSnapshot.gate(app).allows(ProFeature.CAST)) facts = facts or GeodeNative.LINK_ENTITLED
        if (CastStream.lanAddress(app) != null) facts = facts or GeodeNative.LINK_HAS_LAN
        val actions = step(GeodeNative.LINK_EVENT_START, facts)
        if (actions and GeodeNative.LINK_DO_START_STREAM != 0) {
            val refused = CastService.start(app, CastService.Target.LINK)
            if (refused != null) {
                val reason = if (refused is ForegroundServiceStartNotAllowedException) CastFailure.BACKGROUND_START else CastFailure.STREAM
                onFailed(reason.linkReason)
            }
        }
        return actions and GeodeNative.LINK_DO_PAYWALL != 0
    }

    /** The user stopped sharing: the link stops answering, and the phone's sound comes back unless a TV casts. */
    @MainThread
    fun stop(context: Context) {
        if (step(GeodeNative.LINK_EVENT_STOP, 0) and GeodeNative.LINK_DO_STOP_STREAM != 0) {
            CastService.release(context.applicationContext, CastService.Target.LINK)
        }
    }

    /** The UI has shown an [LinkState.Ended] share's reason. */
    @MainThread
    fun shown() {
        step(GeodeNative.LINK_EVENT_SHOWN, 0)
    }

    /**
     * [CastService]: the stream is ready; the page's link as the user sees it is [shareUrl], named [shareLabel],
     * and the short link a computer types is [pairUrl].
     */
    internal fun onReady(
        shareUrl: String,
        shareLabel: String,
        pairUrl: String,
    ) {
        url = shareUrl
        label = shareLabel
        typedUrl = pairUrl
        step(GeodeNative.LINK_EVENT_READY, 0)
    }

    /**
     * The code a computer types after the short link ("ABC-DEF"), "" while nothing is shared: single-session,
     * replaced once used or after a few minutes (native, `geode_link_pair_code`), so the sheet reads it again often.
     */
    fun pairCode(): String = GeodeNative.linkPairCode(SystemClock.elapsedRealtime())

    /** [CastService]: the stream ended by itself, for [reason] (GEODE_LINK_REASON_*). */
    internal fun onFailed(reason: Int) {
        step(GeodeNative.LINK_EVENT_FAILED, reason)
    }

    /** [CastService]: its notification's Stop ended everything. */
    internal fun onServiceStopped() {
        step(GeodeNative.LINK_EVENT_STOP, 0)
    }

    private fun step(
        event: Int,
        arg: Int,
    ): Int {
        val actions = GeodeNative.linkStep(machine, event, arg)
        mutableState.value =
            when (machine[0]) {
                GeodeNative.LINK_PHASE_STARTING -> LinkState.Starting
                GeodeNative.LINK_PHASE_LIVE -> url?.let { LinkState.Live(it, label, typedUrl) } ?: LinkState.Starting
                GeodeNative.LINK_PHASE_ENDED -> LinkState.Ended(machine[1])
                else -> LinkState.Idle
            }
        if (machine[0] != GeodeNative.LINK_PHASE_LIVE) url = null
        return actions
    }
}
