package dev.geode.render.fluid

import dev.geode.analysis.AudioFeatures
import dev.geode.render.LiveSignal
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

internal class FluidChoreography {
    companion object {
        const val MAX_SPAWN = 8
        const val MAX_CATCH = 4

        const val PATH_ORBIT = 0
        const val PATH_LISSAJOUS = 1
        const val PATH_ROSE = 2
        const val PATH_BLOOM = 3
        const val PATH_DRIFT = 4
        val PATH_LABELS = listOf("Orbit", "Lissajous", "Rose", "Bloom", "Drift")

        const val GOLDEN_ANGLE = 2.399963f

        private const val TIME_WRAP_SECONDS = 7100f

        fun sceneSpeed(speed: Float): Float = speed.coerceIn(0.05f, 4f)

        private const val FOLLOW_RATE = 2.2f

        private const val MAX_SPEED = 4.5f

        private const val DOMAIN_MARGIN = 0.92f
    }

    class Anchor {
        var x = 0f
        var y = 0f
        var targetX = 0f
        var targetY = 0f

        var energy = 0f

        fun follow(dt: Float) {
            val k = 1f - kotlin.math.exp(-dt * FOLLOW_RATE)
            var dx = (targetX - x) * k
            var dy = (targetY - y) * k
            val step = sqrt(dx * dx + dy * dy)
            val maxStep = MAX_SPEED * dt
            if (step > maxStep && step > 1e-6f) {
                val s = maxStep / step
                dx *= s
                dy *= s
            }
            x += dx
            y += dy
        }

        fun snap() {
            x = targetX
            y = targetY
        }
    }

    var path = PATH_LISSAJOUS
    var spawnCount = 3
    var catchCount = 2

    var progressionAmount = 1f

    var speed = 1f

    val spawns: List<Anchor> = List(MAX_SPAWN) { Anchor() }
    val catches: List<Anchor> = List(MAX_CATCH) { Anchor() }

    /** Hits heard so far. Drives the bloom path's phyllotaxis. */
    var hitCount = 0
        private set

    private var time = 0f
    private var lastSection = -1
    private var sectionPhase = 0f
    private var initialized = false
    private var beatEnv = 0f
    private var bassEnv = 0f
    private val hitEdge = LiveSignal.Edge()
    private val traverse = LiveSignal.Traverse()

    fun tick(
        f: AudioFeatures,
        dt: Float,
        aspect: Float,
    ) {
        time = (time + dt * (0.4f + 0.6f * speed)) % TIME_WRAP_SECONDS
        if (hitEdge.step(f)) hitCount++
        beatEnv = max(LiveSignal.hit(f), beatEnv * kotlin.math.exp(-dt / 0.35f))
        val bassTarget = (f.bass * 1.2f).coerceIn(0f, 1f)
        bassEnv += (bassTarget - bassEnv) * (if (bassTarget > bassEnv) (dt / 0.03f) else (dt / 0.45f)).coerceAtMost(1f)

        // The journey used to be laid out along the track's play POSITION and re-seated from a
        // pre-analysed section list. That meant nothing moved on live input, a seek teleported
        // every spawn point, and a file the analyser had not reached had exactly one section.
        // Traverse walks on heard energy and re-seats when the spectrum changes character, so
        // the progression keeps its shape without the visuals ever consulting a timeline.
        traverse.step(f, dt)
        if (traverse.sectionCount != lastSection) {
            lastSection = traverse.sectionCount
            sectionPhase = traverse.sectionCount * GOLDEN_ANGLE
        }

        val progress = traverse.position * progressionAmount.coerceIn(0f, 1f)
        val ax = min(aspect, 1.6f) * DOMAIN_MARGIN

        val nS = spawnCount.coerceIn(1, MAX_SPAWN)
        for (i in 0 until nS) {
            val (tx, ty) = spawnTarget(i, nS, progress, ax)
            spawns[i].targetX = tx.coerceIn(-ax, ax)
            spawns[i].targetY = ty.coerceIn(-DOMAIN_MARGIN, DOMAIN_MARGIN)
            val bandE =
                if (f.bands.isEmpty()) {
                    0f
                } else {
                    f.bands[(i * f.bands.size / nS).coerceIn(0, f.bands.size - 1)].coerceIn(0f, 1f)
                }
            spawns[i].energy = (0.5f * beatEnv + 0.5f * bandE).coerceIn(0f, 1f)
        }
        val nC = catchCount.coerceIn(0, MAX_CATCH)
        for (i in 0 until nC) {
            val (tx, ty) = catchTarget(i, nC, progress, ax)
            catches[i].targetX = tx.coerceIn(-ax, ax)
            catches[i].targetY = ty.coerceIn(-DOMAIN_MARGIN, DOMAIN_MARGIN)
            catches[i].energy = bassEnv
        }

        if (!initialized) {
            initialized = true
            spawns.forEach { it.snap() }
            catches.forEach { it.snap() }
        } else {
            spawns.forEach { it.follow(dt) }
            catches.forEach { it.follow(dt) }
        }
    }

    fun reset() {
        initialized = false
        time = 0f
        hitCount = 0
        lastSection = -1
        sectionPhase = 0f
        beatEnv = 0f
        bassEnv = 0f
        hitEdge.reset()
        traverse.reset()
    }

    private fun spawnTarget(
        i: Int,
        n: Int,
        progress: Float,
        ax: Float,
    ): Pair<Float, Float> {
        val frac = i.toFloat() / n
        val precession = sectionPhase + progress * PI.toFloat() + time * 0.13f
        val journeyR = 0.35f + 0.4f * sin(progress * PI.toFloat())
        val cy = (progress - 0.5f) * 0.7f
        return when (path) {
            PATH_ORBIT -> {
                val a = frac * 2f * PI.toFloat() + precession
                (cos(a) * journeyR * ax) to (cy + sin(a) * journeyR)
            }
            PATH_ROSE -> {
                val k = 2f + 3f * progress
                val theta = frac * 2f * PI.toFloat() + precession
                val r = journeyR * (0.35f + 0.65f * abs(cos(k * theta)))
                (cos(theta) * r * ax) to (cy + sin(theta) * r)
            }
            PATH_BLOOM -> {
                val idx = (hitCount + i).toFloat()
                val a = idx * GOLDEN_ANGLE + sectionPhase
                val r = journeyR * sqrt(((idx % 24f) + 1f) / 24f)
                (cos(a) * r * ax) to (cy + sin(a) * r)
            }
            PATH_DRIFT -> {
                val s = i * 3.7f + sectionPhase
                val t = time * 0.31f + progress * 5f
                val x = 0.7f * sin(t * 0.83f + s) * sin(t * 0.19f + s * 1.7f)
                val y = 0.7f * sin(t * 0.67f + s * 2.3f) * sin(t * 0.23f + s)
                (x * ax) to (cy * 0.5f + y * 0.8f)
            }
            else -> {
                val a = 3f + 2f * progress
                val b = 2f + 2f * progress
                val ph = frac * 2f * PI.toFloat() + precession
                (sin(a * time * 0.21f + ph) * journeyR * ax) to
                    (cy + sin(b * time * 0.21f + ph * 1.5f + 1.1f) * journeyR)
            }
        }
    }

    private fun catchTarget(
        i: Int,
        n: Int,
        progress: Float,
        ax: Float,
    ): Pair<Float, Float> {
        val frac = i.toFloat() / n
        val a = frac * 2f * PI.toFloat() + sectionPhase + PI.toFloat() / n - time * 0.09f
        val r = (0.62f - 0.4f * progress).coerceAtLeast(0.12f)
        val cy = (0.5f - progress) * 0.5f
        return (cos(a) * r * ax) to (cy + sin(a) * r * 0.85f)
    }

    fun packSpawns(out: FloatArray) {
        val n = spawnCount.coerceIn(1, MAX_SPAWN)
        for (i in 0 until MAX_SPAWN) {
            val o = i * 4
            if (i < n) {
                out[o] = spawns[i].x
                out[o + 1] = spawns[i].y
                out[o + 2] = 0.4f + 0.6f * spawns[i].energy
                out[o + 3] = 0.05f + 0.10f * spawns[i].energy
            } else {
                out[o] = 0f
                out[o + 1] = 0f
                out[o + 2] = 0f
                out[o + 3] = 0f
            }
        }
    }

    fun packCatches(
        out: FloatArray,
        pull: Float,
        captureRadius: Float,
    ) {
        val n = catchCount.coerceIn(0, MAX_CATCH)
        for (i in 0 until MAX_CATCH) {
            val o = i * 4
            if (i < n) {
                out[o] = catches[i].x
                out[o + 1] = catches[i].y
                out[o + 2] = pull * (0.5f + 0.9f * catches[i].energy)
                out[o + 3] = captureRadius
            } else {
                out[o] = 0f
                out[o + 1] = 0f
                out[o + 2] = 0f
                out[o + 3] = 0f
            }
        }
    }
}
