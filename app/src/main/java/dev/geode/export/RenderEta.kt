package dev.geode.export

class RenderEta(
    private val windowSeconds: Float = 12f,
    private val minProgress: Float = 0.02f,
) {
    private var firstSampleMs = 0L
    private var firstSampleProgress = 0f
    private var latestMs = 0L
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
            latestMs = atMs
            latestProgress = clamped
            return null
        }
        if (clamped < latestProgress) {
            firstSampleMs = atMs
            firstSampleProgress = clamped
        }
        latestMs = atMs
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
        latestMs = 0
        latestProgress = 0f
    }

    companion object {
        fun describe(secondsRemaining: Long): String =
            when {
                secondsRemaining < 30 -> "almost done"
                secondsRemaining < 90 -> "about a minute left"
                secondsRemaining < 3600 -> "about ${(secondsRemaining + 30) / 60} min left"
                else -> "about ${(secondsRemaining + 1800) / 3600} h left"
            }
    }
}
