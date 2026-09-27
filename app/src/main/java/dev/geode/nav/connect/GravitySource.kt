package dev.geode.nav.connect

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Gravity in the device frame, m/s², as the platform reports it: +x right, +y up, +z out of the screen. */
data class Gravity(
    val x: Float,
    val y: Float,
    val z: Float,
) {
    companion object {
        const val STANDARD = 9.80665f

        /** Flat on a table, face up. */
        val REST = Gravity(0f, 0f, STANDARD)
    }
}

/**
 * Feeds tilt into whatever sinks, drifts or flows with it. [start] and [stop] follow the host's
 * lifecycle so a sensor only runs while something is on screen to move.
 */
interface GravitySource {
    val gravity: StateFlow<Gravity>

    fun start()

    fun stop()

    /** No tilt at all; everything sits at [Gravity.REST]. */
    object None : GravitySource {
        override val gravity: StateFlow<Gravity> = MutableStateFlow(Gravity.REST).asStateFlow()

        override fun start() = Unit

        override fun stop() = Unit
    }
}
