package dev.geode.ui.tour

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.core.content.edit
import dev.geode.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * A gesture worth knowing, and how many times someone takes the long way round before the app
 * mentions it: three taps on › before "you can swipe too". Each tip is said once, ever; there is
 * no always-on hint, because a hint that is always there stops being read.
 */
enum class GestureTip(
    val after: Int,
    @param:StringRes val textRes: Int,
) {
    /** Counted by the ‹ › arrows on the visual. */
    SWIPE_STYLES(3, R.string.tour_tip_swipe_styles),

    /** Counted by the sheet's × and the Fullscreen icon. */
    DOUBLE_TAP_SHEET(2, R.string.tour_tip_double_tap_sheet),

    /** Counted by the Record icon on the visual. */
    HOLD_TO_RECORD(1, R.string.tour_tip_hold_to_record),

    /** Counted by the handle pill that brings the faded mini player back. */
    SWIPE_UP_PLAYER(3, R.string.tour_tip_swipe_up_player),
}

/**
 * One-time gesture tips. A control calls [nudge] each time it is used the long way; once a tip's
 * count is reached it shows (one at a time, while no tour runs) and is never shown again.
 *
 * Which tips were seen is kept in a small preferences file of its own, read off the main thread
 * on first use. It is device-local on purpose (not in Backup): a tip learned on one phone is
 * still worth one mention on the next. Until the file is read, [nudge] counts but shows nothing.
 */
object GestureTips {
    private const val FILE = "geode-tour-tips"
    private const val KEY_SEEN = "seen"

    private val counts = mutableMapOf<GestureTip, Int>()
    private var prefs: SharedPreferences? = null
    private var seen: MutableSet<String>? = null

    /** The tip on screen, if any. */
    var showing by mutableStateOf<GestureTip?>(null)
        private set

    fun nudge(tip: GestureTip) {
        val known = seen ?: return
        if (tip.name in known) return
        val n = (counts[tip] ?: 0) + 1
        counts[tip] = n
        if (n >= tip.after && showing == null) showing = tip
    }

    /** The tip was read (Got it, or it timed out): it is never shown again. */
    fun dismiss() {
        val tip = showing ?: return
        showing = null
        seen?.add(tip.name)
        prefs?.edit { putStringSet(KEY_SEEN, seen?.toSet().orEmpty()) }
    }

    /** Settings › Tutorials › Reset all: every tip may be said once more. */
    fun reset(context: Context) {
        counts.clear()
        showing = null
        seen?.clear()
        (prefs ?: context.getSharedPreferences(FILE, Context.MODE_PRIVATE)).edit { remove(KEY_SEEN) }
    }

    internal suspend fun attach(context: Context) {
        if (seen != null) return
        val file = withContext(Dispatchers.IO) { context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE) }
        val read = withContext(Dispatchers.IO) { file.getStringSet(KEY_SEEN, emptySet()).orEmpty().toMutableSet() }
        prefs = file
        seen = read
    }
}

/** Draws the tip being shown, for a few seconds or until Got it. */
@Composable
internal fun GestureTipHost() {
    val tip = GestureTips.showing ?: return
    LaunchedEffect(tip) {
        delay(TIP_VISIBLE_MS)
        GestureTips.dismiss()
    }
    TourHint(
        text = stringResource(tip.textRes),
        actionLabel = stringResource(R.string.tour_tip_got_it),
        onAction = GestureTips::dismiss,
    )
}

private const val TIP_VISIBLE_MS = 7_000L
