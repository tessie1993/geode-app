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

internal class AcidScene(
    private val context: Context,
    private val style: VisualStyleCatalog.AcidStyle,
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = style.id

    private companion object {
        const val SIM_RES = 540

        const val TIME_WRAP_SECONDS = 628.31853f

        const val FEEDBACK_CAP = 0.975f

        const val GLITCH_THRESHOLD = 0.32f

        const val GLITCH_DECAY = 2.4f

        const val ENV_RISE_PER_SEC = 9f
        const val ENV_FALL_PER_SEC = 2.4f

        /** Spokes on the wheel. Matches `uSpokes[12]` in acid_step_frag. */
        const val SPOKES = 12
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
    private var state: FluidBuffers.DoubleFbo? = null

    private val pcmPulse = PcmPulse()
    private var pcmStrike = 0f
    private var envBass = 0f
    private var envMid = 0f
    private var envTreble = 0f
    private var beatPulse = 0f
    private var glitch = 0f
    private var glitchEpoch = 0f
    private var touch: TouchField? = null

    /** Twelve live spectral spokes. Reused every frame — render hot path. */
    private val spokes = FloatArray(SPOKES)

    private val prevFbo = IntArray(1)
    private val prevViewport = IntArray(4)

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        stepProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        state = null
        val quad = GlUtil.loadShader(context, R.raw.quad_vert)
        stepProgram =
            GlUtil.buildProgramReporting(quad, GlUtil.loadShader(context, R.raw.acid_step_frag)) {
                onShaderError("Acid unavailable on this GPU: $it")
            }
        if (stepProgram == 0) return
        showProgram =
            GlUtil.buildProgramReporting(quad, GlUtil.loadShader(context, R.raw.acid_show_frag)) {
                onShaderError("Acid unavailable on this GPU: $it")
            }
        if (showProgram == 0) return
        stepLocs = GlUtil.UniformCache(stepProgram)
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
        state?.release()
        state = null
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
        val fmt = FluidBuffers.TexFormat(GLES30.GL_RGBA8, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE)
        val (w, h) = FluidBuffers.resolution(SIM_RES, width, height)
        val next = FluidBuffers.DoubleFbo(w, h, fmt, linear = true)
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

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        GlUtil.resetFrameState()
        GLES30.glGetIntegerv(GLES30.GL_DRAW_FRAMEBUFFER_BINDING, prevFbo, 0)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, prevViewport, 0)
        val loop = ensureState() ?: return
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
        if (LiveSignal.hit(f) * p.beatResponse > GLITCH_THRESHOLD) {
            glitch = 1f
            glitchEpoch = (glitchEpoch + 1f) % 1024f
        }
        glitch = (glitch - dt * GLITCH_DECAY).coerceAtLeast(0f)
        // The wheel used to prefer the chromagram's pitch classes and only fall back to the
        // band envelopes when a track had not been analysed — so the figure changed shape
        // depending on whether analysis had caught up. It is one live reading now: the
        // current band envelopes folded into twelve spokes, identical on file and on mic.
        fillSpokes(f.bands)

        val frames = dt * 60f
        val feedback = (style.feedback.coerceAtMost(FEEDBACK_CAP)).pow(frames)
        val zoom = style.zoom.pow(frames)
        val rotate = style.rotate * frames * speed
        val hueShift = style.hueRate * dt * speed

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glBindVertexArray(vao)

        GLES30.glUseProgram(stepProgram)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, loop.write.fbo)
        GLES30.glViewport(0, 0, loop.width, loop.height)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, loop.read.tex)
        GLES30.glUniform1i(stepLocs.loc("uPrev"), 0)
        GLES30.glUniform2f(stepLocs.loc("uRes"), loop.width.toFloat(), loop.height.toFloat())
        GLES30.glUniform1i(stepLocs.loc("uStyle"), style.mode)
        GLES30.glUniform1i(stepLocs.loc("uSource"), style.source)
        GLES30.glUniform1f(stepLocs.loc("uZoom"), zoom)
        GLES30.glUniform1f(stepLocs.loc("uRotate"), rotate)
        GLES30.glUniform1f(stepLocs.loc("uHueShift"), hueShift)
        GLES30.glUniform1f(stepLocs.loc("uFeedback"), feedback)
        GLES30.glUniform1f(stepLocs.loc("uModulate"), style.modulate)
        GLES30.glUniform1f(stepLocs.loc("uGlitch"), glitch * style.glitch)
        GLES30.glUniform1f(stepLocs.loc("uEpoch"), glitchEpoch)
        GLES30.glUniform1f(stepLocs.loc("uTime"), time)
        GLES30.glUniform1f(stepLocs.loc("uBass"), envBass)
        GLES30.glUniform1f(stepLocs.loc("uMid"), envMid)
        GLES30.glUniform1f(stepLocs.loc("uTreble"), envTreble)
        GLES30.glUniform1f(stepLocs.loc("uBeat"), beatPulse)
        GLES30.glUniform1f(stepLocs.loc("uStrike"), pcmStrike.coerceIn(0f, 1.5f))
        GLES30.glUniform1f(stepLocs.loc("uDrive"), CymaticsMath.safeDrive(p.audioDrive))
        GLES30.glUniform1fv(stepLocs.loc("uSpokes"), SPOKES, spokes, 0)
        GLES30.glUniform1f(stepLocs.loc("uBaseHue"), FluidHue.base(p.paletteBase) + style.hueOffset)
        GLES30.glUniform1f(stepLocs.loc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange) * style.hueSpan)
        GLES30.glUniform1f(stepLocs.loc("uLiquid"), style.liquid + p.turbulence.coerceIn(0f, 1f) * 0.6f)
        SceneTouch.upload(stepLocs, touch)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        loop.swap()

        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, prevFbo[0])
        GLES30.glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3])
        GLES30.glUseProgram(showProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, loop.read.tex)
        GLES30.glUniform1i(showLocs.loc("uState"), 0)
        GLES30.glUniform2f(showLocs.loc("uRes"), width.toFloat(), height.toFloat())
        GLES30.glUniform1f(showLocs.loc("uScanline"), style.scanline)
        GLES30.glUniform1f(showLocs.loc("uCurve"), style.curve)
        GLES30.glUniform1f(showLocs.loc("uSat"), style.saturation)
        GLES30.glUniform1f(showLocs.loc("uFloorHue"), FluidHue.base(p.paletteBase) + style.hueOffset)
        GLES30.glUniform1f(showLocs.loc("uOverdrive"), style.overdrive)
        GLES30.glUniform1f(showLocs.loc("uHit"), (pcmStrike + 0.5f * beatPulse).coerceIn(0f, 1f))
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
    }

    /** Folds the live band envelopes down onto the wheel, in place. */
    private fun fillSpokes(bands: FloatArray) {
        if (bands.isEmpty()) {
            spokes.fill(0f)
            return
        }
        for (i in 0 until SPOKES) {
            val from = i * bands.size / SPOKES
            val to = (((i + 1) * bands.size / SPOKES).coerceAtMost(bands.size)).coerceAtLeast(from + 1)
            var acc = 0f
            for (b in from until to) acc += bands[b]
            spokes[i] = (acc / (to - from)).coerceIn(0f, 1f)
        }
    }

    override fun release() {
        if (stepProgram != 0) GLES30.glDeleteProgram(stepProgram)
        if (showProgram != 0) GLES30.glDeleteProgram(showProgram)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        state?.release()
        state = null
        stepProgram = 0
        showProgram = 0
        vao = 0
        programOk = false
        stepLocs = GlUtil.UniformCache(0)
        showLocs = GlUtil.UniformCache(0)
    }
}
