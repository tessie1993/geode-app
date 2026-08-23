package dev.geode.render.fluid

import dev.geode.analysis.AudioFeatures
import dev.geode.render.LiveSignal

internal class RippleOverlayDrops {
    companion object {
        const val BEAT_DROPS = 2

        const val SPARKLE_INTERVAL = 6

        const val SPARKLE_THRESHOLD = 0.5f
    }

    private var frame = 0
    private var dropIndex = 0
    private val hitEdge = LiveSignal.Edge()

    fun reset() {
        frame = 0
        dropIndex = 0
        hitEdge.reset()
    }

    fun tick(
        features: AudioFeatures,
        aspect: Float,
        queue: (Float, Float, Float, Float) -> Unit,
    ) {
        frame++
        // Rings drop on the heard transient rather than on a tracked beat, so the overlay
        // works on live input and on material the tracker cannot lock to.
        val hit = LiveSignal.hit(features)
        if (hitEdge.step(features)) {
            val amp = (0.22f + 0.4f * features.bass.coerceIn(0f, 1.5f)) * hit
            repeat(BEAT_DROPS) {
                val (x, y) = RippleMath.overlayDropPosition(dropIndex++, aspect)
                queue(x, y, 0.055f, amp)
            }
        }
        if (features.treble > SPARKLE_THRESHOLD && frame % SPARKLE_INTERVAL == 0) {
            val (x, y) = RippleMath.overlayDropPosition(dropIndex++, aspect)
            queue(x, y, 0.03f, 0.1f * features.treble.coerceAtMost(2f))
        }
    }
}
