package dev.geode.nav.connect

import dev.geode.nav.NavMove
import dev.geode.nav.Point
import dev.geode.nav.Presentation

/**
 * What a move should look like, said in the navigation layer's terms. The design system owns the
 * curves, springs and materials; this only says which kind of surface moves, which way, and from
 * where.
 */
data class Transition(
    val kind: Kind,
    /** The way the outgoing content moves; the incoming content comes from the opposite side. */
    val direction: Direction,
    /** The touch that started the move, when known: where a ripple or an expanding surface begins. */
    val origin: Point?,
    /** True when a drag already moved the surfaces; run on from where they are, not from rest. */
    val gesture: Boolean,
) {
    enum class Kind {
        NONE,
        PAGE,
        SHEET,
        SECTION,
        OVERLAY,
        GATE,
    }

    enum class Direction {
        NONE,
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN,
    }
}

/** Decides the [Transition] for a move. Replace it to change the choreography without touching navigation. */
fun interface TransitionConnector {
    fun resolve(
        move: NavMove,
        motion: MotionPolicy,
    ): Transition
}

/**
 * The default choreography: pages advance and return, sheets rise and fall, sections slide
 * toward the one chosen, overlays and gates rise over everything and drop away. Under reduced
 * motion every move is a [Transition.Kind.NONE], leaving the design system a cut or a fade.
 */
object DefaultTransitions : TransitionConnector {
    override fun resolve(
        move: NavMove,
        motion: MotionPolicy,
    ): Transition {
        if (motion.reducedMotion) {
            return Transition(Transition.Kind.NONE, Transition.Direction.NONE, move.origin, move.gesture)
        }
        val (kind, direction) =
            when (move.kind) {
                NavMove.Kind.PUSH ->
                    if (move.to.current.presentation == Presentation.SHEET) {
                        Transition.Kind.SHEET to Transition.Direction.UP
                    } else {
                        Transition.Kind.PAGE to Transition.Direction.FORWARD
                    }
                NavMove.Kind.POP ->
                    if (move.from.current.presentation == Presentation.SHEET) {
                        Transition.Kind.SHEET to Transition.Direction.DOWN
                    } else {
                        Transition.Kind.PAGE to Transition.Direction.BACKWARD
                    }
                NavMove.Kind.REPLACE -> Transition.Kind.PAGE to Transition.Direction.NONE
                NavMove.Kind.SWITCH ->
                    if (move.to.section.ordinal > move.from.section.ordinal) {
                        Transition.Kind.SECTION to Transition.Direction.LEFT
                    } else {
                        Transition.Kind.SECTION to Transition.Direction.RIGHT
                    }
                NavMove.Kind.OPEN_OVERLAY -> Transition.Kind.OVERLAY to Transition.Direction.UP
                NavMove.Kind.CLOSE_OVERLAY -> Transition.Kind.OVERLAY to Transition.Direction.DOWN
                NavMove.Kind.GATE_UP -> Transition.Kind.GATE to Transition.Direction.UP
                NavMove.Kind.GATE_DOWN -> Transition.Kind.GATE to Transition.Direction.DOWN
            }
        return Transition(kind, direction, move.origin, move.gesture)
    }
}
