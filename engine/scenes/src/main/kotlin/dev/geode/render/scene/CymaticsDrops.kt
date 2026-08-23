package dev.geode.render.scene

import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.sin

class CymaticsDrops {
    companion object {
        const val SLOTS = 6

        const val OMEGA = 2.4f

        const val DECAY_SECONDS = 1.4f

        const val SPAWN_THRESHOLD = 0.3f

        const val COOLDOWN_SECONDS = 0.12f

        const val SILENCE = 0.004f

        const val SPREAD = 1.1f

        private const val TWO_PI = 2f * PI.toFloat()
    }

    val packed = FloatArray(SLOTS * 4)

    private var next = 0
    private var cooldown = 0f
    private var seed = 0

    fun update(
        dt: Float,
        hit: Float,
    ) {
        if (dt <= 0f) return
        cooldown = (cooldown - dt).coerceAtLeast(0f)
        val fade = exp(-dt / DECAY_SECONDS)
        for (i in 0 until SLOTS) {
            val base = i * 4
            if (packed[base + 3] <= 0f) continue
            packed[base + 2] = CymaticsMath.wrapPhase(packed[base + 2] + OMEGA * dt, TWO_PI)
            packed[base + 3] *= fade
            if (packed[base + 3] < SILENCE) packed[base + 3] = 0f
        }
        if (hit > SPAWN_THRESHOLD && cooldown <= 0f) {
            spawn(hit)
            cooldown = COOLDOWN_SECONDS
        }
    }

    val ringing: Boolean
        get() {
            for (i in 0 until SLOTS) {
                if (packed[i * 4 + 3] > 0f) return true
            }
            return false
        }

    fun reset() {
        packed.fill(0f)
        cooldown = 0f
    }

    private fun spawn(strength: Float) {
        val base = next * 4
        next = (next + 1) % SLOTS
        seed++
        packed[base] = (hash(seed) - 0.5f) * 2f * SPREAD
        packed[base + 1] = (hash(seed * 7 + 3) - 0.5f) * 2f * SPREAD
        packed[base + 2] = 0f
        packed[base + 3] = 0.22f + 0.33f * strength.coerceIn(0f, 1.5f)
    }

    private fun hash(n: Int): Float {
        val x = sin(n * 12.9898f) * 43758.547f
        return x - kotlin.math.floor(x)
    }
}
