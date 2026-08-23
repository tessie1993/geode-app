package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import android.util.Log
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.gl.DeviceGl
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.TouchField
import dev.geode.render.compute.SimBuild
import dev.geode.render.compute.SimPass
import dev.geode.render.compute.SimSampling
import dev.geode.render.compute.SimSpec
import dev.geode.render.compute.SimUniformBinder
import dev.geode.render.compute.SimUniforms
import dev.geode.render.fluid.FluidBuffers
import dev.geode.render.fluid.FluidHue
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.sin

/**
 * SILK — three band lanes of dye advected through a smooth velocity field.
 *
 * The step goes through [SimPass], which means it runs as an ES 3.1 compute dispatch on a
 * device that proved it has one and as the ES 3.0 fragment ping-pong everywhere else. This
 * scene cannot tell which it got and deliberately has no way to ask: the choice is made once,
 * inside [SimPass.build] during [init], and there is no `if (hasCompute)` anywhere below.
 *
 * Both paths run `silk_step.glsl` — one file, one simulation. They store into the same texture
 * in the same format, so they round identically; what is left to differ between them is the
 * last few ulp of `sin`, `exp` and `normalize`, which no driver promises to implement the same
 * way in its fragment and compute stages.
 */
internal class SilkScene(
    private val context: Context,
    private val style: VisualStyleCatalog.SilkStyle,
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = style.id

    private companion object {
        const val TAG = "SilkScene"

        const val SIM_RES = 320

        /**
         * The range the pre-scaled `RGBA8` fallback packs into [0, 1], matching the ceiling the
         * step clamps its dye to. Only reaches the shader when the probe found no renderable
         * half-float format; on every other device the layer folds a scale of 1.
         */
        const val BYTE_STATE_SCALE = 8f

        const val TIME_WRAP_SECONDS = 628.31853f

        const val TWO_PI = (2.0 * PI).toFloat()

        const val SEED_EPOCH_SECONDS = 9f

        const val ENV_RISE_PER_SEC = 8f
        const val ENV_FALL_PER_SEC = 2.2f

        const val RING_SPEED = 2.6f
        const val RING_MAX = 3.4f

        const val BEAT_THRESHOLD = 0.28f
    }

    private var params = SceneParams.DEFAULT
    private var pendingFeatures: AudioFeatures? = null
    private val silence = AudioFeatures.empty()
    private var width = 1
    private var height = 1
    private var time = 0f
    private var lastDt = 1f / 60f

    private var sim: SimPass? = null
    private var showProgram = 0
    private var showLocs = GlUtil.UniformCache(0)
    private var programOk = false
    private var vao = 0

    private val pcmPulse = PcmPulse()
    private var pcmStrike = 0f
    private var envBass = 0f
    private var envMid = 0f
    private var envTreble = 0f
    private var beatPulse = 0f
    private var ringRadius = -1f
    private var slabTurn = 0f
    private var foldPhase = 0f
    private var drift = 0f

    // The frame's derived step inputs. Fields rather than locals because they are computed in
    // draw() and read from the binder below, which the layer calls back into from inside
    // step() — after it has bound its own state and before the dispatch or the draw.
    private var stepB = 0f
    private var stepAdvect = 0f
    private var stepDecay = 0f
    private var stepDrive = 0f
    private var stepSeedEpoch = 0f

    private var touch: TouchField? = null

    /**
     * Held in a property, not written at the `step(...)` call site.
     *
     * A lambda literal there captures this scene and allocates one object per frame, which is
     * exactly the per-frame garbage the render loop is written to avoid. Allocated once, at
     * construction, it costs nothing thereafter.
     */
    private val stepBinder = SimUniformBinder { uniforms -> bindStep(uniforms) }

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        sim = null
        showProgram = 0
        vao = 0
        programOk = false
        val spec =
            SimSpec(
                label = id,
                stepBody = GlUtil.loadShader(context, R.raw.silk_step),
                // Every texel back-traces along the flow and samples between texels, every
                // frame. That is the access pattern the packed integer state is worst at and
                // the one a filterable half-float field is for.
                sampling = SimSampling.BETWEEN_TEXELS,
                stateScale = BYTE_STATE_SCALE,
            )
        // The profile is memoised on driver identity and was already resolved by
        // VisualizerRenderer.onSurfaceCreated before any scene existed, so this costs three
        // glGetString calls. The diagnostic below is the ONE line that says which path this
        // family took and why — build() runs once per surface, so it is logged once per
        // surface, not re-decided or re-logged per frame. It goes to logcat rather than to
        // onShaderError because taking the fragment path is not an error and a compute step
        // that failed to compile is not one either; both leave a correct picture on screen.
        val built =
            SimPass.build(spec, DeviceGl.profileWithCurrentContext(context)) { line ->
                Log.i(TAG, line)
            }
        val pass =
            when (built) {
                is SimBuild.Failed -> {
                    onShaderError("Silk unavailable on this GPU: ${built.message}")
                    return
                }

                is SimBuild.Ready -> built.pass
            }
        sim = pass
        applySimSize(pass)
        showProgram =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.quad_vert),
                pass.displayShader(GlUtil.loadShader(context, R.raw.silk_show)),
            ) {
                onShaderError("Silk unavailable on this GPU: $it")
            }
        if (showProgram == 0) return
        showLocs = GlUtil.UniformCache(showProgram)
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]
        programOk = true
    }

    override fun setParams(params: SceneParams) {
        this.params = params
    }

    override fun setTouchField(field: TouchField) {
        touch = field
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        this.width = max(width, 1)
        this.height = max(height, 1)
        // The layer reallocates lazily on the next step, so this is safe before there is
        // anything to allocate into — including on a resize that arrives before init().
        sim?.let { applySimSize(it) }
    }

    override fun acceptPcm(
        samples: FloatArray,
        count: Int,
    ) = pcmPulse.accept(samples, count)

    override fun update(
        features: AudioFeatures,
        dt: Float,
    ) {
        time = (time + dt) % TIME_WRAP_SECONDS
        lastDt = dt
        pcmStrike = pcmPulse.tick(dt)
        pendingFeatures = features
    }

    /**
     * The simulation grid, which is not the display grid.
     *
     * A field sim at native resolution is texture-fetch bound on a mid-tier GPU long before it
     * is ALU bound, and the dye is soft by nature — 320 on the long axis is plenty, and the
     * short axis follows the surface so the flow is not stretched.
     */
    private fun applySimSize(pass: SimPass) {
        val (w, h) = FluidBuffers.resolution(SIM_RES, width, height)
        pass.resize(w, h)
    }

    private fun slew(
        current: Float,
        target: Float,
        dt: Float,
    ): Float {
        val limit = if (target > current) ENV_RISE_PER_SEC else ENV_FALL_PER_SEC
        val step = (target - current).coerceIn(-limit * dt, limit * dt)
        return current + step
    }

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        val pass = sim ?: return
        GlUtil.resetFrameState()
        val p = params
        val dt = lastDt.coerceIn(0f, 1f / 15f)
        val f = pendingFeatures ?: silence
        pendingFeatures = null

        val speed = p.speed.coerceIn(0.05f, 4f)
        envBass = slew(envBass, f.bass.coerceIn(0f, 1.5f), dt)
        envMid = slew(envMid, f.mid.coerceIn(0f, 1.5f), dt)
        envTreble = slew(envTreble, f.treble.coerceIn(0f, 1.5f), dt)
        beatPulse =
            maxOf(LiveSignal.hit(f) * p.beatResponse.coerceIn(0f, 2f), beatPulse - dt * 3f)
                .coerceIn(0f, 1.5f)
        if (LiveSignal.hit(f) * p.beatResponse > BEAT_THRESHOLD) ringRadius = 0f
        if (ringRadius >= 0f) {
            ringRadius += dt * RING_SPEED * speed
            if (ringRadius > RING_MAX) ringRadius = -1f
        }

        slabTurn = (slabTurn + dt * style.slabRate * speed) % 1f
        foldPhase = (foldPhase + dt * 0.03f * speed * TWO_PI) % TWO_PI
        drift = (drift + dt * 0.05f * speed) % 1024f
        stepB = style.bBase + style.bAmp * sin(TWO_PI * time / style.bPeriod)
        stepSeedEpoch = (time / SEED_EPOCH_SECONDS).toInt().toFloat()
        stepAdvect = dt * 0.18f * style.flow * speed
        stepDrive = CymaticsMath.safeDrive(p.audioDrive)

        var decay = style.decay
        if (p.trails) decay += (1f - decay) * 0.6f * p.trailLength.coerceIn(0f, 1f)
        stepDecay = decay.pow(dt * 60f)

        // The step restores whatever draw target and viewport it found on the fragment path,
        // and touches neither on the compute path, so the present pass below lands on the
        // renderer's target without this scene saving or rebinding anything.
        if (!pass.step(stepBinder)) return

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glBindVertexArray(vao)
        GLES30.glUseProgram(showProgram)
        pass.bindStateFor(showLocs, 0)
        GLES30.glUniform2f(showLocs.loc("uRes"), width.toFloat(), height.toFloat())
        GLES30.glUniform1f(showLocs.loc("uBaseHue"), FluidHue.base(p.paletteBase) + style.hueOffset)
        GLES30.glUniform1f(showLocs.loc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange) * style.hueSpan)
        GLES30.glUniform1f(showLocs.loc("uExposure"), style.exposure)
        GLES30.glUniform1i(showLocs.loc("uFold"), style.fold)
        GLES30.glUniform1f(showLocs.loc("uFoldPhase"), foldPhase)
        GLES30.glUniform1f(showLocs.loc("uEnergy"), f.rms.coerceIn(0f, 1.5f))
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
    }

    /**
     * The step's own uniforms, set through the layer rather than by hand.
     *
     * Nothing here names a texture unit or a state sampler: unit 0 belongs to the state and
     * [SimUniforms] hands out the rest by name, which is what makes a body that grows a second
     * input unable to collide with the layer's own binding.
     */
    private fun bindStep(uniforms: SimUniforms) {
        uniforms.int("uField", style.field)
        uniforms.float("uB", stepB)
        uniforms.float("uAdvect", stepAdvect)
        uniforms.float("uDecay", stepDecay)
        uniforms.float("uFieldScale", style.fieldScale)
        uniforms.float("uSwirl", style.swirl)
        uniforms.float("uSlabX", cos(slabTurn * TWO_PI))
        uniforms.float("uSlabY", sin(slabTurn * TWO_PI))
        uniforms.float("uSeedEpoch", stepSeedEpoch)
        uniforms.float("uDrift", drift)
        uniforms.float("uStrokes", style.strokes)
        uniforms.float("uElong", style.elong)
        uniforms.float("uDrive", stepDrive)
        uniforms.float("uBass", envBass)
        uniforms.float("uMid", envMid)
        uniforms.float("uTreble", envTreble)
        uniforms.float("uBeat", beatPulse)
        uniforms.float("uStrike", pcmStrike.coerceIn(0f, 1.5f))
        uniforms.float("uBeatRing", ringRadius)
        SceneTouch.upload(uniforms, touch)
    }

    override fun release() {
        sim?.release()
        sim = null
        if (showProgram != 0) GLES30.glDeleteProgram(showProgram)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        showProgram = 0
        vao = 0
        programOk = false
        showLocs = GlUtil.UniformCache(0)
    }
}
