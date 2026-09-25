package dev.geode.ui.opaline

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.withFrameNanos
import kotlinx.coroutines.flow.collectLatest
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.min

/**
 * Port of `Spring` in `src/motion.js`: a damped oscillator on a semantic 0-1 coordinate, advanced
 * in sub-steps of at most 1/240 s with each frame's delta capped at 1/20 s.
 */
class OpalineSpring(
    value: Float,
    private val frequency: Float,
    private val damping: Float,
) {
    var value: Float = value
        private set
    var velocity: Float = 0f
        private set
    var target: Float = value

    val settled: Boolean
        get() = abs(target - value) < SETTLE_DISTANCE && abs(velocity) < SETTLE_VELOCITY

    fun step(dt: Float): Float {
        val h = min(dt, MAX_FRAME)
        val n = ceil(h / SUB_STEP).toInt()
        if (n <= 0) return value
        val d = h / n
        val w = frequency * 2f * PI.toFloat()
        repeat(n) {
            velocity += (w * w * (target - value) - 2f * damping * w * velocity) * d
            value += velocity * d
        }
        return value
    }

    fun reset(to: Float) {
        target = to
        value = to
        velocity = 0f
    }

    private companion object {
        const val MAX_FRAME = 1f / 20f
        const val SUB_STEP = 1f / 240f
        const val SETTLE_DISTANCE = 0.0005f
        const val SETTLE_VELOCITY = 0.001f
    }
}

/**
 * Compose state over an [OpalineSpring]. It only runs frames while unsettled, so a resting control
 * does no work; with reduced motion it lands on the target immediately.
 */
@Stable
class OpalineSpringState internal constructor(
    initial: Float,
    frequency: Float,
    damping: Float,
) {
    private val spring = OpalineSpring(initial, frequency, damping)
    var target by mutableFloatStateOf(initial)
    var value by mutableFloatStateOf(initial)
        private set
    var velocity by mutableFloatStateOf(0f)
        private set
    internal var reducedMotion by mutableStateOf(false)

    fun snapTo(to: Float) {
        spring.reset(to)
        target = to
        value = to
        velocity = 0f
    }

    internal suspend fun run() {
        snapshotFlow { target to reducedMotion }.collectLatest { (goal, reduced) ->
            spring.target = goal
            if (reduced) {
                snapTo(goal)
                return@collectLatest
            }
            var last = -1L
            while (!spring.settled) {
                withFrameNanos { now ->
                    val dt = if (last < 0) OpalineMotion.FIXED_DT else (now - last) / 1_000_000_000f
                    last = now
                    spring.step(dt)
                    value = spring.value
                    velocity = spring.velocity
                }
            }
            snapTo(goal)
        }
    }
}

@Composable
fun rememberOpalineSpring(
    target: Float,
    frequency: Float = OpalineMotion.PRESS_FREQUENCY,
    damping: Float = OpalineMotion.PRESS_DAMPING,
): OpalineSpringState {
    val reduced = LocalOpalineReducedMotion.current
    val state = remember { OpalineSpringState(target, frequency, damping) }
    SideEffect {
        state.target = target
        state.reducedMotion = reduced
    }
    LaunchedEffect(state) { state.run() }
    return state
}
