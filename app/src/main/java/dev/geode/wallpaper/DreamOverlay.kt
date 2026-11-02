package dev.geode.wallpaper

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.text.TextUtils
import android.text.format.DateFormat
import android.util.TypedValue
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextClock
import android.widget.TextView
import dev.geode.audio.NowPlayingBridge
import dev.geode.widget.WidgetState
import java.util.Locale
import kotlin.random.Random

/**
 * What the dream shows over the visual: the time, the date under it, and what is playing.
 *
 * Centred, large and thin, like Android's own clock dream. Plain framework views rather than Compose: a dream has no `ComponentActivity` to host a
 * composition, and text that changes once a minute needs nothing a [TextView] lacks. The system's
 * own 12/24-hour choice comes free with [TextClock] (both formats left unset), and the date
 * pattern is the locale's best one for weekday, day and month.
 *
 * Now playing is MuzViz's own track first ([WidgetState], which the playback service publishes on
 * every player event, so reading it starts no session), else whatever another app plays through
 * [NowPlayingBridge], which only answers once the user has given notification access. Nothing
 * playing, the line goes away. It is read every [REFRESH_MS] while the dream runs: a lookup in a
 * cached prefs file and, with access, one binder call.
 *
 * It drifts. A dream runs for hours on a charger, and text that never moves burns into an OLED
 * panel; once a minute it steps to a new spot within [DRIFT_DP] of its home, the same trick the
 * system's ambient clock uses. The shadow keeps white text readable over a bright frame.
 */
class DreamOverlay(
    context: Context,
    /** Settings › Behaviour's dream toggles (`BehaviourPrefs.dreamShowClock`, `dreamShowNowPlaying`). */
    private val showClock: Boolean,
    private val showNowPlaying: Boolean,
) : LinearLayout(context) {
    private val handler = Handler(Looper.getMainLooper())
    private val driftPx = dp(DRIFT_DP)
    private val nowPlayingBridge = NowPlayingBridge(context)
    private val nowPlaying = line(NOW_PLAYING_SP, Typeface.create("sans-serif-medium", Typeface.NORMAL))

    private val drift =
        object : Runnable {
            override fun run() {
                translationX = Random.nextFloat() * 2f * driftPx - driftPx
                translationY = Random.nextFloat() * 2f * driftPx - driftPx
                handler.postDelayed(this, DRIFT_EVERY_MS)
            }
        }

    private val refresh =
        object : Runnable {
            override fun run() {
                showNowPlaying()
                handler.postDelayed(this, REFRESH_MS)
            }
        }

    init {
        orientation = VERTICAL
        gravity = Gravity.CENTER_HORIZONTAL
        alpha = OVERLAY_ALPHA
        if (showClock) {
            addView(clock(TIME_SP, Typeface.create("sans-serif-light", Typeface.NORMAL)))
            addView(
                clock(DATE_SP, Typeface.create("sans-serif", Typeface.NORMAL)).apply {
                    val pattern = DateFormat.getBestDateTimePattern(Locale.getDefault(), "EEEdMMM")
                    format12Hour = pattern
                    format24Hour = pattern
                },
            )
        }
        if (showNowPlaying) {
            nowPlaying.apply {
                maxLines = 1
                ellipsize = TextUtils.TruncateAt.END
                maxWidth = (resources.displayMetrics.widthPixels * NOW_PLAYING_WIDTH).toInt()
                setPadding(0, dp(NOW_PLAYING_GAP_DP).toInt(), 0, 0)
                visibility = GONE
            }
            addView(nowPlaying)
        }
    }

    /** Starts the drift and the now-playing refresh; the clocks tick on their own while attached. */
    fun start() {
        stop()
        handler.post(drift)
        if (showNowPlaying) handler.post(refresh)
    }

    fun stop() {
        handler.removeCallbacks(drift)
        handler.removeCallbacks(refresh)
    }

    private fun showNowPlaying() {
        val own = WidgetState.load(context).takeIf { it.playing && it.title.isNotBlank() }
        val text =
            if (own != null) {
                joined(own.title, own.artist)
            } else {
                nowPlayingBridge
                    .current()
                    ?.takeIf { it.playing && it.title.isNotBlank() }
                    ?.let { joined(it.title, it.artist) }
            }
        nowPlaying.text = text.orEmpty()
        nowPlaying.visibility = if (text == null) GONE else VISIBLE
    }

    private fun joined(
        title: String,
        artist: String,
    ): String = if (artist.isBlank()) title else "$title · $artist"

    private fun clock(
        sizeSp: Float,
        face: Typeface,
    ): TextClock = TextClock(context).apply { style(sizeSp, face) }

    private fun line(
        sizeSp: Float,
        face: Typeface,
    ): TextView = TextView(context).apply { style(sizeSp, face) }

    private fun TextView.style(
        sizeSp: Float,
        face: Typeface,
    ) {
        setTextColor(Color.WHITE)
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        typeface = face
        gravity = Gravity.CENTER_HORIZONTAL
        setShadowLayer(dp(SHADOW_DP), 0f, 0f, Color.argb(SHADOW_ALPHA, 0, 0, 0))
    }

    private fun dp(value: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics)

    private companion object {
        const val TIME_SP = 72f
        const val DATE_SP = 18f
        const val NOW_PLAYING_SP = 16f
        const val NOW_PLAYING_GAP_DP = 12f

        /** Of the screen's width: a long title ends in an ellipsis rather than running off. */
        const val NOW_PLAYING_WIDTH = 0.8f
        const val OVERLAY_ALPHA = 0.8f
        const val SHADOW_DP = 8f
        const val SHADOW_ALPHA = 160
        const val DRIFT_DP = 24f
        const val DRIFT_EVERY_MS = 60_000L
        const val REFRESH_MS = 5_000L
    }
}
