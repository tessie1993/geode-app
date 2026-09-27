package dev.geode.nav.connect

import dev.geode.nav.NavMove
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Everything the design system plugs into the shell, in one place. The navigation layer draws
 * and animates nothing itself; each of these is a seam a design-system implementation fills,
 * and the defaults do nothing, so the shell runs with no design system attached.
 */
class NavConnectors(
    val motion: StateFlow<MotionPolicy>,
    val transitions: TransitionConnector = DefaultTransitions,
    val touch: TouchFeedback = TouchFeedback.None,
    val gravity: GravitySource = GravitySource.None,
    val haptics: HapticConnector = HapticConnector.None,
) {
    /** The transition for [move] under the motion policy in force right now. */
    fun transition(move: NavMove): Transition = transitions.resolve(move, motion.value)

    companion object {
        /** Full motion, nothing attached. */
        fun none(): NavConnectors = NavConnectors(motion = MutableStateFlow(MotionPolicy()))
    }
}
