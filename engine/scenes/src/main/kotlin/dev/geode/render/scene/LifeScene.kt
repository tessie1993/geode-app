package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.TouchField
import dev.geode.render.fluid.FluidBuffers
import dev.geode.render.fluid.FluidHue
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sin

internal class LifeScene(
    private val context: Context,
    private val style: VisualStyleCatalog.LifeStyle,
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = style.id

    private companion object {
        const val SIM_RES = 288

        const val TIME_WRAP_SECONDS = 628.31853f

        const val SEED_SECONDS = 0.5f

        const val GOLDEN_ANGLE = 2.399963f

        const val BEAT_THRESHOLD = 0.3f

        const val CENSUS_SECONDS = 4f
        const val STARVED = 0.004f
        const val OVERGROWN = 0.985f

        val CENSUS_PROBES = arrayOf(intArrayOf(1, 2), intArrayOf(3, 2), intArrayOf(2, 1), intArrayOf(2, 3), intArrayOf(2, 2))

        const val ENV_RISE_PER_SEC = 9f
        const val ENV_FALL_PER_SEC = 2.4f
    }

    private var params = SceneParams.DEFAULT
    private var pendingFeatures: AudioFeatures? = null
    private val silence = AudioFeatures.empty()
    private var width = 1
    private var height = 1
    private var time = 0f
    private var lastDt = 1f / 60f

    private var stepProgram = 0
    private var showProgram = 0
    private var stepLocs = GlUtil.UniformCache(0)
    private var showLocs = GlUtil.UniformCache(0)
    private var programOk = false
    private var vao = 0
    private var formats: FluidBuffers.Formats? = null
    private var state: FluidBuffers.DoubleFbo? = null
    private var byteState = false

    private val pcmPulse = PcmPulse()
    private var pcmStrike = 0f
    private var envTreble = 0f
    private var beatPulse = 0f
    private var seedRemain = 0f
    private var kick = 0f
    private var kickAngle = 0f
    private var kickX = 0.5f
    private var kickY = 0.5f
    private var censusAge = 0f
    private var touch: TouchField? = null
    private val censusBytes =
        java.nio.ByteBuffer
            .allocateDirect(16)
            .order(java.nio.ByteOrder.nativeOrder())
    private val censusBuf = censusBytes.asFloatBuffer()

    private val prevFbo = IntArray(1)
    private val prevViewport = IntArray(4)

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        stepProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        formats = null
        state = null
        val quad = GlUtil.loadShader(context, R.raw.quad_vert)
        stepProgram =
            GlUtil.buildProgramReporting(quad, GlUtil.loadShader(context, R.raw.life_step_frag)) {
                onShaderError("Life unavailable on this GPU: $it")
            }
        if (stepProgram == 0) return
        showProgram =
            GlUtil.buildProgramReporting(quad, GlUtil.loadShader(context, R.raw.life_show_frag)) {
                onShaderError("Life unavailable on this GPU: $it")
            }
        if (showProgram == 0) return
        stepLocs = GlUtil.UniformCache(stepProgram)
        showLocs = GlUtil.UniformCache(showProgram)
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]
        programOk = true
        seedRemain = SEED_SECONDS
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
        state?.release()
        state = null
        seedRemain = SEED_SECONDS
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

    private fun ensureState(): FluidBuffers.DoubleFbo? {
        state?.let { return it }
        val fmt = formats ?: FluidBuffers.probeFormats().also { formats = it }
        byteState = !fmt.ok
        val texFmt =
            if (byteState) {
                FluidBuffers.TexFormat(GLES30.GL_RGBA8, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE)
            } else {
                fmt.rgba
            }
        val (w, h) = FluidBuffers.resolution(SIM_RES, width, height)
        val next = FluidBuffers.DoubleFbo(w, h, texFmt, linear = true)
        next.create()
        return if (next.ok) {
            state = next
            next
        } else {
            next.release()
            null
        }
    }

    private fun slew(
        current: Float,
        target: Float,
        dt: Float,
    ): Float {
        val limit = if (target > current) ENV_RISE_PER_SEC else ENV_FALL_PER_SEC
        return current + (target - current).coerceIn(-limit * dt, limit * dt)
    }

    private fun census(
        field: FluidBuffers.DoubleFbo,
        restoreFbo: Int,
    ) {
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, field.read.fbo)
        var maxA = 0f
        var maxV = 0f
        var minLive = Float.MAX_VALUE
        var sane = true
        for (probe in CENSUS_PROBES) {
            val x = field.width * probe[0] / 4
            val y = field.height * probe[1] / 4
            val a: Float
            val v: Float
            if (byteState) {
                censusBytes.clear()
                GLES30.glReadPixels(x, y, 1, 1, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, censusBytes)
                a = (censusBytes.get(0).toInt() and 0xFF) / 255f
                v = (censusBytes.get(1).toInt() and 0xFF) / 255f
            } else {
                censusBuf.clear()
                GLES30.glReadPixels(x, y, 1, 1, GLES30.GL_RGBA, GLES30.GL_FLOAT, censusBuf)
                a = censusBuf.get(0)
                v = censusBuf.get(1)
            }
            sane = sane && a.isFinite() && v.isFinite()
            maxA = max(maxA, a)
            maxV = max(maxV, v)
            minLive = minOf(minLive, if (style.rule == 0) a else v)
        }
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, restoreFbo)
        if (!sane) return
        val maxLive = if (style.rule == 0) maxA else maxV
        val starving = maxLive < STARVED && (if (style.rule == 1) maxA > 0.9f else true)
        if (starving || minLive > OVERGROWN) seedRemain = SEED_SECONDS
    }

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        GlUtil.resetFrameState()
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevViewport, 0)
        val field = ensureState() ?: return
        val p = params
        val dt = lastDt.coerceIn(0f, 1f / 15f)
        val f = pendingFeatures ?: silence
        pendingFeatures = null

        val speed = p.speed.coerceIn(0.05f, 4f)
        val drive = CymaticsMath.safeDrive(p.audioDrive)
        envTreble = slew(envTreble, f.treble.coerceIn(0f, 1.5f), dt)
        val hit = LiveSignal.hit(f)
        beatPulse =
            maxOf(hit * p.beatResponse.coerceIn(0f, 2f), beatPulse - dt * 3f)
                .coerceIn(0f, 1.5f)
        kick = (kick - dt * 5f).coerceAtLeast(0f)
        if (hit * p.beatResponse > BEAT_THRESHOLD) {
            kick = (0.4f + 0.6f * hit.coerceIn(0f, 1.5f)) * drive
            kickAngle += GOLDEN_ANGLE
            kickX = 0.5f + 0.32f * cos(kickAngle)
            kickY = 0.5f + 0.32f * sin(kickAngle)
        }
        seedRemain = (seedRemain - dt).coerceAtLeast(0f)
        censusAge += dt
        if (censusAge >= CENSUS_SECONDS) {
            censusAge = 0f
            census(field, prevFbo[0])
        }

        val substeps = (style.substeps * speed).roundToInt().coerceIn(1, 8)

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glBindVertexArray(vao)

        GLES30.glUseProgram(stepProgram)
        GLES30.glViewport(0, 0, field.width, field.height)
        GLES30.glUniform2f(stepLocs.loc("uRes"), field.width.toFloat(), field.height.toFloat())
        GLES30.glUniform1i(stepLocs.loc("uRule"), style.rule)
        GLES30.glUniform1f(stepLocs.loc("uDt"), style.dt)
        GLES30.glUniform1i(stepLocs.loc("uCore"), style.core)
        GLES30.glUniform1i(stepLocs.loc("uGrowth"), style.growth)
        GLES30.glUniform1f(stepLocs.loc("uMu"), style.mu)
        GLES30.glUniform1f(stepLocs.loc("uSigma"), style.sigma)
        GLES30.glUniform1f(stepLocs.loc("uRadius"), style.radius)
        GLES30.glUniform1i(stepLocs.loc("uRings"), style.rings)
        GLES30.glUniform3f(stepLocs.loc("uB"), style.b1, style.b2, style.b3)
        GLES30.glUniform1f(stepLocs.loc("uF"), style.feed)
        GLES30.glUniform1f(stepLocs.loc("uK"), style.kill)
        GLES30.glUniform2f(stepLocs.loc("uDiff"), 1.0f, 0.5f)
        GLES30.glUniform1f(stepLocs.loc("uAniso"), style.aniso)
        GLES30.glUniform1f(stepLocs.loc("uSeedJitter"), style.seedJitter)
        GLES30.glUniform1f(stepLocs.loc("uTime"), time)
        repeat(substeps) { pass ->
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, field.write.fbo)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, field.read.tex)
            GLES30.glUniform1i(stepLocs.loc("uPrev"), 0)
            val first = pass == 0
            GLES30.glUniform1f(stepLocs.loc("uSeed"), if (first) (seedRemain / SEED_SECONDS) else 0f)
            GLES30.glUniform1f(stepLocs.loc("uKick"), if (first) kick else 0f)
            GLES30.glUniform2f(stepLocs.loc("uKickPos"), kickX, kickY)
            GLES30.glUniform1f(stepLocs.loc("uSprinkle"), if (first) (envTreble + pcmStrike * 0.5f) * drive else 0f)
            // Injections land once per frame, not once per substep: at Speed 4 this pass runs
            // eight times, and a touch integrated eight times would seed eight times as hard
            // for the same gesture.
            SceneTouch.upload(stepLocs, touch, enabled = first)
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
            field.swap()
        }
        kick = 0f

        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3])
        GLES30.glUseProgram(showProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, field.read.tex)
        GLES30.glUniform1i(showLocs.loc("uState"), 0)
        GLES30.glUniform2f(showLocs.loc("uRes"), width.toFloat(), height.toFloat())
        GLES30.glUniform2f(showLocs.loc("uSimRes"), field.width.toFloat(), field.height.toFloat())
        GLES30.glUniform1i(showLocs.loc("uLook"), style.look)
        GLES30.glUniform1f(showLocs.loc("uShowV"), if (style.rule == 1) 1f else 0f)
        GLES30.glUniform1f(showLocs.loc("uBaseHue"), FluidHue.base(p.paletteBase) + style.hueOffset)
        GLES30.glUniform1f(showLocs.loc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange) * style.hueSpan)
        GLES30.glUniform1f(showLocs.loc("uEnergy"), f.rms.coerceIn(0f, 1.5f))
        GLES30.glUniform1f(showLocs.loc("uBeat"), beatPulse)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
    }

    override fun release() {
        if (stepProgram != 0) GLES30.glDeleteProgram(stepProgram)
        if (showProgram != 0) GLES30.glDeleteProgram(showProgram)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        state?.release()
        state = null
        formats = null
        stepProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        stepLocs = GlUtil.UniformCache(0)
        showLocs = GlUtil.UniformCache(0)
    }
}
