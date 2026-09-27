package dev.geode.nav.connect

import dev.geode.nav.Point

/** One touch as the feedback layer sees it, in root-relative pixels. */
data class TouchEvent(
    val phase: Phase,
    val at: Point,
    /** Movement since the previous event of this touch; zero except while dragging. */
    val delta: Point = Point.ZERO,
    /** Time since the press, so a hold or a stretch can grow with it. */
    val elapsedMs: Long = 0L,
) {
    enum class Phase {
        PRESS,
        HOLD,
        DRAG,
        RELEASE,
        CANCEL,
    }
}

/**
 * Where ripples, ink, glow, stretch and every other touch response plug in. The navigation layer
 * never calls this: the design system's touch modifier feeds it, and anything shared — a water
 * field, a particle layer, a physics world — listens. [None] until one exists.
 */
fun interface TouchFeedback {
    fun onTouch(event: TouchEvent)

    companion object {
        val None = TouchFeedback { }
    }
}
