package dev.geode.export

import android.content.res.Resources
import androidx.annotation.PluralsRes
import dev.geode.R

class RenderEta(
    private val windowSeconds: Float = 12f,
    private val minProgress: Float = 0.02f,
) {
    private var firstSampleMs = 0L
    private var firstSampleProgress = 0f
    private var latestProgress = 0f
    private var started = false

    @Suppress("ReturnCount")
    fun sample(
        progress: Float,
        atMs: Long,
    ): Long? {
        val clamped = progress.coerceIn(0f, 1f)
        if (!started) {
            started = true
            firstSampleMs = atMs
            firstSampleProgress = clamped
            latestProgress = clamped
            return null
        }
        if (clamped < latestProgress) {
            firstSampleMs = atMs
            firstSampleProgress = clamped
        }
        latestProgress = clamped

        val windowMs = (windowSeconds * 1000).toLong()
        if (atMs - firstSampleMs > windowMs * 2) {
            val carried = (latestProgress - firstSampleProgress) * windowMs / (atMs - firstSampleMs).toFloat()
            firstSampleMs = atMs - windowMs
            firstSampleProgress = latestProgress - carried
        }

        val elapsedMs = atMs - firstSampleMs
        val gained = latestProgress - firstSampleProgress
        if (elapsedMs < windowMs || gained < minProgress) return null
        val remaining = 1f - latestProgress
        if (remaining <= 0f) return 0L
        val msPerUnit = elapsedMs / gained
        return (remaining * msPerUnit / 1000f).toLong().coerceAtLeast(0L)
    }

    fun reset() {
        started = false
        firstSampleMs = 0
        firstSampleProgress = 0f
        latestProgress = 0f
    }

    companion object {
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
