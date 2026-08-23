package dev.geode.engine.audio

class BarTracker {
    var barPhase: Float = 0f
        private set

    var beatInBar: Int = 0
        private set

    var downbeat: Boolean = false
        private set

    var confidence: Float = 0f
        private set

    private var prevPhase = 0f
    private var beatIndex = 0
    private var downbeatPos = 0
    private var beatsSeen = 0
    private val scores = FloatArray(BEATS_PER_BAR)

    fun step(
        phase: Float,
        beat: Boolean,
        locked: Boolean,
        accent: Float,
    ) {
        if (phase < prevPhase - WRAP_THRESHOLD) {
            beatIndex = (beatIndex + 1) % BEATS_PER_BAR
            for (i in scores.indices) scores[i] *= SCORE_LEAK
        }
        prevPhase = phase

        var beatSlot = beatIndex
        if (beat && accent > 0f) {
            if (phase > 0.5f) beatSlot = (beatIndex + 1) % BEATS_PER_BAR
            scores[beatSlot] += accent
            if (beatsSeen < BEATS_PER_BAR) beatsSeen++
            elect()
        }

        beatInBar = (beatIndex - downbeatPos + BEATS_PER_BAR) % BEATS_PER_BAR
        barPhase = (beatInBar + phase.coerceIn(0f, 1f)) / BEATS_PER_BAR
        downbeat = beat && beatSlot == downbeatPos && beatsSeen >= BEATS_PER_BAR
        confidence = if (locked) clarity() else 0f
    }

    fun reset() {
        prevPhase = 0f
        beatIndex = 0
        downbeatPos = 0
        beatsSeen = 0
        scores.fill(0f)
        barPhase = 0f
        beatInBar = 0
        downbeat = false
        confidence = 0f
    }

    private fun elect() {
        var best = downbeatPos
        for (i in scores.indices) {
            if (scores[i] > scores[best]) best = i
        }
        if (best != downbeatPos && scores[best] > scores[downbeatPos] * SWITCH_MARGIN) {
            downbeatPos = best
        }
    }

    private fun clarity(): Float {
        var best = 0f
        var second = 0f
        for (s in scores) {
            if (s > best) {
                second = best
                best = s
            } else if (s > second) {
                second = s
            }
        }
        return if (best <= 1e-6f) 0f else ((best - second) / best).coerceIn(0f, 1f)
    }

    companion object {
        const val BEATS_PER_BAR: Int = 4

        const val WRAP_THRESHOLD: Float = 0.5f

        const val SCORE_LEAK: Float = 0.969f

        const val SWITCH_MARGIN: Float = 1.25f
    }
}
