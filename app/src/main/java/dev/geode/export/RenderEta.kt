package dev.geode.export

import android.content.res.Resources
import androidx.annotation.PluralsRes
import dev.geode.R
import kotlin.math.exp
import kotlin.math.roundToLong

/**
 * Time left for the running export, from progress samples.
 *
 * The bar is not one steady job: track analysis and the AAC transcode race through their share
 * of it, then the GPU render crawls through the rest. The old sliding-window rate carried that
 * early sprint into the render, so the estimate started near "about a minute", climbed to the
 * real figure as the window caught up, and only then counted down (QA #24). Now the caller says
 * which samples are the steady render (`steady`), and only those count:
 *
 * - Nothing is shown until the render has run for [warmupMs] (shader compile and encoder start
 *   are a slow first frame or two), then for [minSpanMs] more with at least [minProgress] gained.
 * - The rate is the mean since that baseline, not a recent window, so it settles rather than
 *   swings as one heavy passage of the scene comes and goes.
 * - The shown value counts down by wall time between samples and eases toward the fresh figure
 *   with time constant [smoothingMs], so it never jumps on a single sample.
 *
 * A non-steady sample, or progress moving backwards (a new run, an encoder retry), starts over.
 */
class RenderEta(
    private val warmupMs: Long = 4_000L,
    private val minSpanMs: Long = 6_000L,
    private val minProgress: Float = 0.01f,
    private val smoothingMs: Long = 8_000L,
) {
    private var steadySinceMs = NONE
    private var baseMs = NONE
    private var baseProgress = 0f
    private var latestProgress = 0f
    private var shownSeconds: Float? = null
    private var shownAtMs = 0L

    /** Seconds left, or null while there is not yet enough steady progress to say. */
    fun sample(
        progress: Float,
        atMs: Long,
        steady: Boolean = true,
    ): Long? {
        val clamped = progress.coerceIn(0f, 1f)
        if (!steady || clamped < latestProgress) {
            reset()
            latestProgress = clamped
            return null
        }
        latestProgress = clamped
        if (clamped >= 1f) return 0L
        if (steadySinceMs == NONE) steadySinceMs = atMs
        if (baseMs == NONE) {
            if (atMs - steadySinceMs < warmupMs) return null
            baseMs = atMs
            baseProgress = clamped
            return null
        }
        val spanMs = atMs - baseMs
        val gained = clamped - baseProgress
        if (spanMs < minSpanMs || gained < minProgress) return null
        val fresh = (1f - clamped) * spanMs / gained / 1000f
        val previous = shownSeconds
        val next =
            if (previous == null) {
                fresh
            } else {
                val dtMs = (atMs - shownAtMs).coerceAtLeast(0L)
                val countedDown = (previous - dtMs / 1000f).coerceAtLeast(0f)
                val pull = 1f - exp(-dtMs.toFloat() / smoothingMs)
                countedDown + (fresh - countedDown) * pull
            }
        shownSeconds = next
        shownAtMs = atMs
        return next.roundToLong().coerceAtLeast(0L)
    }

    fun reset() {
        steadySinceMs = NONE
        baseMs = NONE
        baseProgress = 0f
        latestProgress = 0f
        shownSeconds = null
        shownAtMs = 0L
    }

    companion object {
        private const val NONE = -1L

        fun describe(
            res: Resources,
            secondsRemaining: Long,
        ): String =
            when {
                secondsRemaining < 30 -> res.getString(R.string.export_eta_almost_done)
                secondsRemaining < 90 -> res.getString(R.string.export_eta_one_minute)
                secondsRemaining < 3600 -> count(res, R.plurals.export_eta_minutes, (secondsRemaining + 30) / 60)
                else -> count(res, R.plurals.export_eta_hours, (secondsRemaining + 1800) / 3600)
            }

        private fun count(
            res: Resources,
            @PluralsRes id: Int,
            n: Long,
        ): String = res.getQuantityString(id, n.toInt(), n.toInt())
    }
}
