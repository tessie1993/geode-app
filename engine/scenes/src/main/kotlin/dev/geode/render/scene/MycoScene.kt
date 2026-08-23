package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.TouchField
import dev.geode.render.fluid.FluidBuffers
import dev.geode.render.fluid.FluidHue
import kotlin.math.max
import kotlin.math.pow

internal class MycoScene(
    private val context: Context,
    private val style: VisualStyleCatalog.MycoStyle,
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = style.id

    private companion object {
        const val TRAIL_RES = 384

        const val TIME_WRAP_SECONDS = 628.31853f

        const val BEAT_THRESHOLD = 0.3f

        const val ENV_RISE_PER_SEC = 9f
        const val ENV_FALL_PER_SEC = 2.4f

        const val BYTE_FALLBACK_DEPOSIT = 0.125f

        /**
         * Fraction of the colony reborn under a finger per second of contact.
         *
         * Every agent relocated to the fingertip is an agent that stopped reinforcing
         * the road it was on, and the trail behind it decays in well under a second on
         * most styles. So this is a budget balanced against the decay, not against how
         * visible the burst is: at 0.35/s a second of contact moves about 29% of the
         * population, which is enough to see a colony grow out of the touch and few
         * enough that the network it grew out of is still there when it does. At 1.0/s
         * the picture visibly resets, which reads as a bug rather than as a gesture.
         */
        const val TOUCH_BIRTH_PER_SECOND = 0.35f

        /**
         * Ceiling on one frame's share of that budget.
         *
         * dt is already clamped to 1/15 s upstream, so this only binds on a stalled
         * frame — where a single un-capped frame would relocate a visible slab of the
         * colony at once and the burst would look like a stutter instead of a spray.
         */
        const val TOUCH_BIRTH_PER_FRAME_CAP = 0.02f
    }

    private var params = SceneParams.DEFAULT
    private var pendingFeatures: AudioFeatures? = null
    private val silence = AudioFeatures.empty()
    private var width = 1
    private var height = 1
    private var time = 0f
    private var lastDt = 1f / 60f

    private var agentProgram = 0
    private var depositProgram = 0
    private var blurProgram = 0
    private var showProgram = 0
    private var agentLocs = GlUtil.UniformCache(0)
    private var depositLocs = GlUtil.UniformCache(0)
    private var blurLocs = GlUtil.UniformCache(0)
    private var showLocs = GlUtil.UniformCache(0)
    private var programOk = false
    private var vao = 0

    private var formats: FluidBuffers.Formats? = null
    private var agents: FluidBuffers.DoubleFbo? = null
    private var trail: FluidBuffers.DoubleFbo? = null
    private var byteTrail = false
    private var agentsSeeded = false

    private val pcmPulse = PcmPulse()
    private var pcmStrike = 0f
    private var envBass = 0f
    private var envTreble = 0f
    private var beatPulse = 0f
    private var reaim = 0f
    private var touch: TouchField? = null

    private val prevFbo = IntArray(1)
    private val prevViewport = IntArray(4)

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        agentProgram = 0
        depositProgram = 0
        blurProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        formats = null
        agents = null
        trail = null
        agentsSeeded = false
        val quad = GlUtil.loadShader(context, R.raw.quad_vert)
        val fail = { what: String -> { msg: String? -> onShaderError("Mycelium $what unavailable: $msg") } }
        agentProgram =
            GlUtil.buildProgramReporting(
                quad,
                GlUtil.loadShader(context, R.raw.myco_agent_frag),
                fail("agents"),
            )
        depositProgram =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.myco_deposit_vert),
                GlUtil.loadShader(context, R.raw.myco_deposit_frag),
                fail("deposit"),
            )
        blurProgram =
            GlUtil.buildProgramReporting(
                quad,
                GlUtil.loadShader(context, R.raw.myco_blur_frag),
                fail("trail"),
            )
        showProgram =
            GlUtil.buildProgramReporting(
                quad,
                GlUtil.loadShader(context, R.raw.myco_show_frag),
                fail("present"),
            )
        val programs = intArrayOf(agentProgram, depositProgram, blurProgram, showProgram)
        if (programs.any { it == 0 }) return
        agentLocs = GlUtil.UniformCache(agentProgram)
        depositLocs = GlUtil.UniformCache(depositProgram)
        blurLocs = GlUtil.UniformCache(blurProgram)
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
        trail?.release()
        trail = null
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

    private fun ensureBuffers(): Boolean {
        val fmt = formats ?: FluidBuffers.probeFormats().also { formats = it }
        if (agents == null) {
            val agentFmt =
                fmt.rgba32
                    ?: if (fmt.ok) {
                        fmt.rgba
                    } else {
                        FluidBuffers.TexFormat(GLES30.GL_RGBA8, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE)
                    }
            val next = FluidBuffers.DoubleFbo(style.agentRes, style.agentRes, agentFmt, linear = false)
            next.create()
            if (next.ok) {
                agents = next
                agentsSeeded = false
            } else {
                next.release()
            }
        }
        if (agents != null && trail == null) {
            val (w, h) = FluidBuffers.resolution(TRAIL_RES, width, height)
            byteTrail = !fmt.ok
            val trailFmt =
                if (byteTrail) {
                    FluidBuffers.TexFormat(GLES30.GL_RGBA8, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE)
                } else {
                    fmt.rg
                }
            val next = FluidBuffers.DoubleFbo(w, h, trailFmt, linear = true)
            next.create()
            if (next.ok) trail = next else next.release()
        }
        return agents != null && trail != null
    }

    private fun slew(
        current: Float,
        target: Float,
        dt: Float,
    ): Float {
        val limit = if (target > current) ENV_RISE_PER_SEC else ENV_FALL_PER_SEC
        return current + (target - current).coerceIn(-limit * dt, limit * dt)
    }

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        GlUtil.resetFrameState()
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevViewport, 0)
        if (!ensureBuffers()) return
        val colony = checkNotNull(agents)
        val field = checkNotNull(trail)
        val p = params
        val dt = lastDt.coerceIn(0f, 1f / 15f)
        val f = pendingFeatures ?: silence
        pendingFeatures = null

        val speed = p.speed.coerceIn(0.05f, 4f)
        val drive = CymaticsMath.safeDrive(p.audioDrive)
        envBass = slew(envBass, f.bass.coerceIn(0f, 1.5f), dt)
        envTreble = slew(envTreble, f.treble.coerceIn(0f, 1.5f), dt)
        beatPulse =
            maxOf(LiveSignal.hit(f) * p.beatResponse.coerceIn(0f, 2f), beatPulse - dt * 3f)
                .coerceIn(0f, 1.5f)
        reaim = if (LiveSignal.hit(f) * p.beatResponse > BEAT_THRESHOLD) style.reaim else 0f

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glBindVertexArray(vao)

        GLES30.glUseProgram(agentProgram)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, colony.write.fbo)
        GLES30.glViewport(0, 0, colony.width, colony.height)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, colony.read.tex)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, field.read.tex)
        GLES30.glUniform1i(agentLocs.loc("uAgents"), 0)
        GLES30.glUniform1i(agentLocs.loc("uTrail"), 1)
        GLES30.glUniform2f(agentLocs.loc("uTrailRes"), field.width.toFloat(), field.height.toFloat())
        GLES30.glUniform1f(agentLocs.loc("uInit"), if (agentsSeeded) 0f else 1f)
        GLES30.glUniform1f(agentLocs.loc("uSpeciesMix"), style.speciesMix)
        GLES30.glUniform1f(agentLocs.loc("uSensorDist"), style.sensorDist)
        GLES30.glUniform1f(agentLocs.loc("uSensorAngle"), style.sensorAngle)
        GLES30.glUniform1f(agentLocs.loc("uTurnAngle"), style.turnAngle)
        GLES30.glUniform1f(agentLocs.loc("uMoveStep"), style.moveStep * speed * (1f + 0.5f * envBass * drive))
        GLES30.glUniform4f(agentLocs.loc("uMatrix"), style.selfA, style.crossAb, style.crossBa, style.selfB)
        GLES30.glUniform1f(agentLocs.loc("uBreath"), beatPulse * drive)
        GLES30.glUniform1f(agentLocs.loc("uJitter"), style.jitter + 0.35f * envTreble * drive + p.turbulence.coerceIn(0f, 1f) * 0.5f)
        GLES30.glUniform1f(agentLocs.loc("uSnap"), style.snap)
        GLES30.glUniform1f(agentLocs.loc("uReaim"), reaim)
        GLES30.glUniform1f(agentLocs.loc("uTime"), time)
        GLES30.glUniform1f(agentLocs.loc("uAniso"), style.aniso)
        // A rate integrated over dt, never a per-frame constant: otherwise one gesture buds
        // twice as many agents at 120 Hz as it does at 60.
        val birthRate = (TOUCH_BIRTH_PER_SECOND * dt).coerceAtMost(TOUCH_BIRTH_PER_FRAME_CAP)
        GLES30.glUniform1f(agentLocs.loc("uTouchBirth"), birthRate * (touch?.anchorStrength ?: 0f))
        SceneTouch.upload(agentLocs, touch)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        colony.swap()
        agentsSeeded = true

        GLES30.glUseProgram(depositProgram)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, field.read.fbo)
        GLES30.glViewport(0, 0, field.width, field.height)
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, colony.read.tex)
        GLES30.glUniform1i(depositLocs.loc("uAgents"), 0)
        GLES30.glUniform2f(depositLocs.loc("uAgentRes"), colony.width.toFloat(), colony.height.toFloat())
        val deposit = style.deposit * if (byteTrail) BYTE_FALLBACK_DEPOSIT else 1f
        GLES30.glUniform1f(depositLocs.loc("uDeposit"), deposit)
        GLES30.glDrawArrays(GLES30.GL_POINTS, 0, colony.width * colony.height)
        GLES30.glDisable(GLES30.GL_BLEND)

        GLES30.glUseProgram(blurProgram)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, field.write.fbo)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, field.read.tex)
        GLES30.glUniform1i(blurLocs.loc("uTrail"), 0)
        GLES30.glUniform2f(blurLocs.loc("uTrailRes"), field.width.toFloat(), field.height.toFloat())
        GLES30.glUniform1f(blurLocs.loc("uDecay"), style.decay.pow(dt * 60f))
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        field.swap()

        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3])
        GLES30.glUseProgram(showProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, field.read.tex)
        GLES30.glUniform1i(showLocs.loc("uTrail"), 0)
        GLES30.glUniform2f(showLocs.loc("uRes"), width.toFloat(), height.toFloat())
        GLES30.glUniform2f(showLocs.loc("uTrailRes"), field.width.toFloat(), field.height.toFloat())
        GLES30.glUniform1i(showLocs.loc("uLook"), style.look)
        GLES30.glUniform1f(showLocs.loc("uBaseHue"), FluidHue.base(p.paletteBase) + style.hueOffset)
        GLES30.glUniform1f(showLocs.loc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange) * style.hueSpan)
        val exposure = style.exposure * if (byteTrail) 1f / BYTE_FALLBACK_DEPOSIT else 1f
        GLES30.glUniform1f(showLocs.loc("uExposure"), exposure)
        GLES30.glUniform1f(showLocs.loc("uEnergy"), f.rms.coerceIn(0f, 1.5f))
        GLES30.glUniform1f(showLocs.loc("uBeat"), beatPulse)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
    }

    override fun release() {
        if (agentProgram != 0) GLES30.glDeleteProgram(agentProgram)
        if (depositProgram != 0) GLES30.glDeleteProgram(depositProgram)
        if (blurProgram != 0) GLES30.glDeleteProgram(blurProgram)
        if (showProgram != 0) GLES30.glDeleteProgram(showProgram)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        agents?.release()
        trail?.release()
        agents = null
        trail = null
        formats = null
        agentProgram = 0
        depositProgram = 0
        blurProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        agentLocs = GlUtil.UniformCache(0)
        depositLocs = GlUtil.UniformCache(0)
        blurLocs = GlUtil.UniformCache(0)
        showLocs = GlUtil.UniformCache(0)
    }
}
