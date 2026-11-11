package dev.geode

import android.os.SystemClock

/**
 * Remembers that Android ended a render at the foreground-service time limit (API 35+ gives a
 * media-processing or data-sync service a few hours a day in the background, then calls
 * `Service.onTimeout`), so the render's end can say so.
 *
 * WHY: the only way to stop a render is to cancel it, and a cancelled render ends silently, as if
 * the user had pressed Cancel. Someone who left a long render running came back to nothing and no
 * reason. The service marks the limit here before it cancels; whatever shows the cancelled outcome
 * asks [justReached].
 *
 * The mark is a time, not a flag, and expires after [WINDOW_MS]: nothing has to clear it, so a
 * render that ended some other way in the same instant cannot leave it behind to mislabel the
 * next Cancel, and reading it twice gives the same answer.
 */
object ExportTimeLimit {
    /** Longer than any render takes to wind down after a cancel, far shorter than starting another. */
    const val WINDOW_MS: Long = 30_000L

    @Volatile
    private var reachedAtMs: Long? = null

    /** The service's `onTimeout`: marks the limit when a render is [running], then runs [cancel]. */
    fun reached(
        running: Boolean,
        cancel: () -> Unit,
        nowMs: Long = SystemClock.elapsedRealtime(),
    ) {
        if (running) reachedAtMs = nowMs
        cancel()
    }

    /**
     * Whether a render ending cancelled now (or at [nowMs]) was stopped by the time limit, not by
     * the user. The clock is read only when there is a mark, so plain JVM tests of the callers
     * never touch it.
     */
    fun justReached(nowMs: Long? = null): Boolean {
        val reachedAt = reachedAtMs ?: return false
        return (nowMs ?: SystemClock.elapsedRealtime()) - reachedAt in 0L until WINDOW_MS
    }
}
