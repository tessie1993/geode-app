package dev.geode.render.fluid

import dev.geode.analysis.AudioFeatures
import dev.geode.render.LiveSignal
import dev.geode.render.scene.SceneParams
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

internal class FluidEmitters(
    private val random: Random = Random.Default,
) {
    companion object {
        const val PATTERN_CENTER = 0
        const val PATTERN_RING = 1
        const val PATTERN_RANDOM = 2
        const val PATTERN_SPECTRUM_ARC = 3

        const val BASE_SPEED = 6f
        private const val MAX_SPLATS_PER_FRAME = 16

        private const val PHASE_WRAP_SECONDS = 628.31853f

        private const val TWO_PI = (2.0 * Math.PI).toFloat()

        const val MIN_BEAT_RESPONSE = 0f
        const val MAX_BEAT_RESPONSE = 2f

        const val BEAT_RESPONSE_GATE = 0.05f

        private const val MAX_RADIUS_SWELL = 2f
    }

    var choreography: FluidChoreography? = null

    var beatPattern = PATTERN_RING
    var beatSplats = 3
    var stirrers = 2
    var stirrerSpeed = 1.0f
    var bassPump = false
    var sparkle = true
    var splatRadius = 0.12f
    var radiusPulse = 0.4f
    var paletteCycleSpeed = 0.5f

    var forceScale = 1f

    var catchSuction = 1f

    var beatResponse = 1f

    var beatEnv = 0f
        private set

    fun applyParams(p: SceneParams) {
        beatPattern = p.fluidBeatPattern.coerceIn(0, 3)
        beatSplats = p.fluidBeatSplats.coerceIn(0, 8)
        stirrers = p.fluidStirrers.coerceIn(0, 4)
        stirrerSpeed = p.fluidStirrerSpeed.coerceIn(0f, 2f) * FluidChoreography.sceneSpeed(p.speed)
        bassPump = p.fluidBassPump
        sparkle = p.fluidSparkle
        splatRadius = p.fluidSplatRadius.coerceIn(0.02f, 0.4f)
        radiusPulse = p.fluidRadiusPulse.coerceIn(0f, 1f)
        catchSuction = p.fluidCatchPull.coerceIn(0f, 3f)
        beatResponse = p.beatResponse
    }

    private var beatEnvRaw = 0f

    var bassEnv = 0f
        private set

    private val stirrerAngle = FloatArray(4) { it * 1.7f }
    private val stirrerPrevX = FloatArray(4) { Float.NaN }
    private val stirrerPrevY = FloatArray(4) { Float.NaN }
    private var activeStirrers = 0
    private var trebleMean = 0.05f
    private var palettePhase = 0f
    private var suctionPhase = 0f
    private var suctionIndex = 0
    private val hitEdge = LiveSignal.Edge()

    private val bands = FloatArray(4)
    private var anchorX = 0f
    private var anchorY = 0f
    private val splatRgb = FloatArray(3)

    fun tick(
        f: AudioFeatures,
        dt: Float,
        aspect: Float,
        baseHue: Float,
        hueSpan: Float,
    ): List<FluidSim.Splat> {
        val out = ArrayList<FluidSim.Splat>(MAX_SPLATS_PER_FRAME)
        tick(f, dt, aspect, baseHue, hueSpan, out)
        return out
    }

    fun tick(
        f: AudioFeatures,
        dt: Float,
        aspect: Float,
        baseHue: Float,
        hueSpan: Float,
        out: MutableList<FluidSim.Splat>,
    ) {
        out.clear()
        beatEnvRaw = max(LiveSignal.hit(f), beatEnvRaw * exp(-dt / 0.3f))
        beatEnv = beatEnvRaw * beatResponse.coerceIn(MIN_BEAT_RESPONSE, MAX_BEAT_RESPONSE)
        val bassTarget = (f.bass * 1.2f).coerceIn(0f, 1f)
        bassEnv +=
            (bassTarget - bassEnv) *
            (if (bassTarget > bassEnv) (dt / 0.03f) else (dt / 0.4f)).coerceAtMost(1f)
        trebleMean += (f.treble - trebleMean) * (dt / 0.32f).coerceAtMost(1f)
        palettePhase = (palettePhase + dt * paletteCycleSpeed * 0.05f) % 1f
        suctionPhase = (suctionPhase + dt) % PHASE_WRAP_SECONDS

        val radius = splatRadius * (1f + radiusPulse * beatEnv).coerceAtMost(MAX_RADIUS_SWELL)
        val speed = BASE_SPEED * forceScale * (0.4f + 1.6f * f.bass) * (0.3f + 0.7f * beatEnv)

        // Fires on the transient that was heard, not on the tempo grid's next slot: the
        // splat lands with the hit instead of a fraction of a bar later, and material with no
        // steady pulse still throws splats.
        val beatEdge = hitEdge.step(f)

        if (beatEdge && beatSplats > 0 && beatResponse > BEAT_RESPONSE_GATE) {
            beatSplats(out, f, aspect, baseHue, hueSpan, radius, speed)
        }
        stirrerSplats(out, f, dt, aspect, baseHue, hueSpan, radius)
        suctionSplats(out, radius)
        if (sparkle && f.treble > trebleMean * 1.6f && f.treble > 0.08f) {
            sparkleSplats(out, aspect, baseHue, hueSpan, radius)
        }
        if (bassPump && bassEnv > 0.15f) pumpSplats(out, baseHue, hueSpan, radius)

        while (out.size > MAX_SPLATS_PER_FRAME) out.removeAt(out.size - 1)
    }

    private fun anchor(
        i: Int,
        aspect: Float,
    ) {
        val c = choreography
        if (c != null) {
            val n = c.spawnCount.coerceIn(1, FluidChoreography.MAX_SPAWN)
            val a = c.spawns[i % n]
            anchorX = a.x
            anchorY = a.y
            return
        }
        val ang = suctionPhase * 0.4f + i * 2.1f
        anchorX = cos(ang) * 0.45f * aspect.coerceAtMost(1.4f)
        anchorY = sin(ang) * 0.45f
    }

    private fun stirrerSplats(
        out: MutableList<FluidSim.Splat>,
        f: AudioFeatures,
        dt: Float,
        aspect: Float,
        baseHue: Float,
        hueSpan: Float,
        radius: Float,
    ) {
        val n = stirrers.coerceIn(0, 4)
        if (n != activeStirrers) {
            for (i in 0 until 4) {
                stirrerPrevX[i] = Float.NaN
                stirrerPrevY[i] = Float.NaN
            }
            activeStirrers = n
        }
        if (n == 0) return
        bands[0] = f.bass
        bands[1] = f.mid
        bands[2] = f.treble
        bands[3] = f.rms
        for (i in 0 until n) {
            val band = bands[i % bands.size]
            anchor(i, aspect)
            val cxA = anchorX
            val cyA = anchorY
            val orbitR = 0.14f + 0.10f * (i % 3)
            stirrerAngle[i] =
                (stirrerAngle[i] + dt * stirrerSpeed * (0.3f + band * 1.7f) * (if (i % 2 == 0) 1f else -1f)) % TWO_PI
            val x = cxA + cos(stirrerAngle[i]) * orbitR
            val y = cyA + sin(stirrerAngle[i]) * orbitR
            val px = stirrerPrevX[i]
            val py = stirrerPrevY[i]
            if (!px.isNaN()) {
                val invDt = 1f / dt.coerceAtLeast(1e-3f)
                hsv((baseHue + palettePhase + i * hueSpan / 4f) % 1f, 0.85f, 1f, splatRgb)
                val cr = splatRgb[0]
                val cg = splatRgb[1]
                val cb = splatRgb[2]
                val amp = 0.1f + 0.55f * band
                out +=
                    FluidSim.Splat(
                        prevX = px,
                        prevY = py,
                        curX = x,
                        curY = y,
                        radius = radius,
                        velX = (x - px) * invDt * 0.22f * forceScale,
                        velY = (y - py) * invDt * 0.22f * forceScale,
                        r = cr * amp,
                        g = cg * amp,
                        b = cb * amp,
                    )
            }
            stirrerPrevX[i] = x
            stirrerPrevY[i] = y
        }
    }

    private fun beatSplats(
        out: MutableList<FluidSim.Splat>,
        f: AudioFeatures,
        aspect: Float,
        baseHue: Float,
        hueSpan: Float,
        radius: Float,
        speed: Float,
    ) {
        val n = beatSplats.coerceIn(1, 8)
        val dyeGain = 1.5f * (0.15f + 0.85f * beatEnv)
        for (i in 0 until n) {
            val frac = i / n.toFloat()
            val hue = (baseHue + palettePhase + frac * hueSpan) % 1f
            hsv(hue, 0.9f, 1f, splatRgb)
            val cr = splatRgb[0]
            val cg = splatRgb[1]
            val cb = splatRgb[2]
            anchor(i, aspect)
            val ax = anchorX
            val ay = anchorY
            when (beatPattern) {
                PATTERN_CENTER -> {
                    val a = frac * 2f * PI.toFloat() + palettePhase * 6f
                    out +=
                        capsule(
                            ax,
                            ay,
                            ax + cos(a) * 0.06f,
                            ay + sin(a) * 0.06f,
                            radius,
                            cos(a) * speed,
                            sin(a) * speed,
                            cr,
                            cg,
                            cb,
                            dyeGain,
                        )
                }
                PATTERN_RANDOM -> {
                    val x = ax + (random.nextFloat() * 2f - 1f) * 0.25f
                    val y = ay + (random.nextFloat() * 2f - 1f) * 0.25f
                    val a = random.nextFloat() * 2f * PI.toFloat()
                    out +=
                        capsule(
                            x,
                            y,
                            x + cos(a) * 0.05f,
                            y + sin(a) * 0.05f,
                            radius,
                            cos(a) * speed,
                            sin(a) * speed,
                            cr,
                            cg,
                            cb,
                            dyeGain,
                        )
                }
                PATTERN_SPECTRUM_ARC -> {
                    val bandE =
                        if (f.bands.isEmpty()) {
                            0.5f
                        } else {
                            val bandIdx = (frac * (f.bands.size - 1)).toInt().coerceIn(0, f.bands.size - 1)
                            f.bands[bandIdx].coerceIn(0f, 1.5f)
                        }
                    val x = (frac * 2f - 1f) * 0.7f * aspect
                    val y = (ay * 0.35f - 0.6f).coerceIn(-0.9f, -0.35f)
                    val v = speed * (0.4f + 1.6f * bandE) / (0.4f + 1.6f * f.bass).coerceAtLeast(0.4f)
                    out += capsule(x, y, x, y + 0.06f, radius, 0f, v, cr, cg, cb, dyeGain * (0.4f + bandE))
                }
                else -> {
                    val a = frac * 2f * PI.toFloat() + palettePhase * 6f
                    val ringR = 0.16f
                    val x = ax + cos(a) * ringR
                    val y = ay + sin(a) * ringR
                    val tx = -sin(a)
                    val ty = cos(a)
                    out +=
                        capsule(
                            x - tx * 0.04f,
                            y - ty * 0.04f,
                            x + tx * 0.04f,
                            y + ty * 0.04f,
                            radius,
                            tx * speed,
                            ty * speed,
                            cr,
                            cg,
                            cb,
                            dyeGain,
                        )
                }
            }
        }
    }

    private fun suctionSplats(
        out: MutableList<FluidSim.Splat>,
        radius: Float,
    ) {
        val c = choreography ?: return
        if (catchSuction <= 0f) return
        val n = c.catchCount.coerceIn(0, FluidChoreography.MAX_CATCH)
        if (n == 0) return
        suctionIndex = (suctionIndex + 1) % n
        val a = c.catches[suctionIndex]
        val strength = BASE_SPEED * 0.7f * catchSuction * (0.35f + 0.65f * bassEnv)
        val ang = suctionPhase * 2.7f + suctionIndex * 2.1f
        val ox = cos(ang) * 0.18f
        val oy = sin(ang) * 0.18f
        val len = sqrt(ox * ox + oy * oy).coerceAtLeast(1e-4f)
        out +=
            FluidSim.Splat(
                prevX = a.x + ox,
                prevY = a.y + oy,
                curX = a.x,
                curY = a.y,
                radius = radius * 0.8f,
                velX = -ox / len * strength,
                velY = -oy / len * strength,
                r = 0f,
                g = 0f,
                b = 0f,
            )
    }

    private fun sparkleSplats(
        out: MutableList<FluidSim.Splat>,
        aspect: Float,
        baseHue: Float,
        hueSpan: Float,
        radius: Float,
    ) {
        repeat(1 + random.nextInt(2)) {
            anchor(random.nextInt(8), aspect)
            val ax = anchorX
            val ay = anchorY
            val x = ax + (random.nextFloat() * 2f - 1f) * 0.3f
            val y = ay + (random.nextFloat() * 2f - 1f) * 0.3f
            val a = random.nextFloat() * 2f * PI.toFloat()
            hsv((baseHue + palettePhase + hueSpan * 0.5f) % 1f, 0.35f, 1f, splatRgb)
            val cr = splatRgb[0]
            val cg = splatRgb[1]
            val cb = splatRgb[2]
            out +=
                capsule(
                    x,
                    y,
                    x + cos(a) * 0.02f,
                    y + sin(a) * 0.02f,
                    radius * 0.35f,
                    cos(a) * BASE_SPEED * 0.5f,
                    sin(a) * BASE_SPEED * 0.5f,
                    cr,
                    cg,
                    cb,
                    0.9f,
                )
        }
    }

    private fun pumpSplats(
        out: MutableList<FluidSim.Splat>,
        baseHue: Float,
        hueSpan: Float,
        radius: Float,
    ) {
        val v = BASE_SPEED * forceScale * bassEnv
        anchor(0, 1f)
        val ax = anchorX
        val ay = anchorY
        for (i in 0 until 6) {
            val a = i / 6f * 2f * PI.toFloat()
            hsv((baseHue + palettePhase + i * hueSpan / 6f) % 1f, 0.95f, 1f, splatRgb)
            val cr = splatRgb[0]
            val cg = splatRgb[1]
            val cb = splatRgb[2]
            out +=
                capsule(
                    ax + cos(a) * 0.06f,
                    ay + sin(a) * 0.06f,
                    ax + cos(a) * 0.14f,
                    ay + sin(a) * 0.14f,
                    radius,
                    cos(a) * v,
                    sin(a) * v,
                    cr,
                    cg,
                    cb,
                    0.3f + 0.9f * bassEnv,
                )
        }
    }

    private fun capsule(
        px: Float,
        py: Float,
        cx: Float,
        cy: Float,
        radius: Float,
        vx: Float,
        vy: Float,
        r: Float,
        g: Float,
        b: Float,
        dyeGain: Float,
    ) = FluidSim.Splat(px, py, cx, cy, radius, vx, vy, r * dyeGain, g * dyeGain, b * dyeGain)

    internal fun hsv(
        h: Float,
        s: Float,
        v: Float,
    ): Triple<Float, Float, Float> = FluidHue.hsv(h, s, v)

    private fun hsv(
        h: Float,
        s: Float,
        v: Float,
        out: FloatArray,
    ) = FluidHue.hsv(h, s, v, out)
}
