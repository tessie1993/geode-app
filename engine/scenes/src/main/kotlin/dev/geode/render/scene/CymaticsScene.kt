package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.LiveSignal
import dev.geode.render.TouchField
import dev.geode.render.fluid.FluidHue
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.max
import kotlin.math.sin

internal class CymaticsScene(
    private val context: Context,
    private val style: VisualStyleCatalog.CymaticsStyle =
        requireNotNull(VisualStyleCatalog.cymatics(SceneIds.CYMATICS)),
) : Scene,
    PcmSink,
    TouchReactive {
    override val id: String = style.id

    private companion object {
        const val DRIVE_GAIN = 1.5f

        const val IDLE_RMS = 0.015f

        const val IDLE_FADE_SECONDS = 1.2f

        const val IDLE_SWEEP_HZ = 0.035f

        const val DEFAULT_BAND_COUNT = 64

        const val MIN_COLOR_AMPLITUDE = 0.12f

        const val EXPOSURE = 1.6f

        const val TIME_WRAP_SECONDS = 628.31853f

        const val TWO_PI = (2.0 * PI).toFloat()

        const val DRIFT_WRAP = 2f

        const val TRAVEL_OMEGA = 1.1f

        const val DRIFT_RATE = 0.05f

        const val STYLE_FARADAY = 4

        const val TONE_TAU_SECONDS = 2.5f

        const val TONE_HUE_SPAN = 0.05f

        const val PCM_STRIKE_GAIN = 0.6f

        /**
         * Bounds on the finger's ripple wavenumber, radians per field unit.
         *
         * The driver radiates at the plate's own wavenumber, so the ripples land on the
         * same scale as the figure they are disturbing. The floor matters more than the
         * ceiling: with Audio drive at zero nothing rings, the dominant wavenumber is 0,
         * and an unbounded k would put a single flat lobe under the finger instead of
         * rings. The ceiling keeps the highest modes from driving ripples finer than the
         * screen can resolve, which is aliasing, not detail.
         */
        const val MIN_TOUCH_K = 5f
        const val MAX_TOUCH_K = 26f
    }

    private val plate = CymaticsPlate()

    private val drops = CymaticsDrops()

    private val modes = FloatArray(CymaticsMath.MAX_RENDERED_MODES * 4)
    private var modeCount = 0

    private var params = SceneParams.DEFAULT
    private var time = 0f
    private var lastDt = 1f / 60f
    private var pendingFeatures: AudioFeatures? = null
    private var width = 1
    private var height = 1

    private var program = 0
    private var uniforms = GlUtil.UniformCache(0)
    private var programOk = false
    private var vao = 0

    private var beatPulse = 0f

    private val pcmPulse = PcmPulse()
    private var pcmStrike = 0f

    private var swirlPhase = 0f

    private var travelPhase = 0f

    private var driftShift = 0f

    private var toneHue = 0f

    private var idleBlend = 0f
    private var idlePhase = 0f

    private var touch: TouchField? = null

    /**
     * The finger's ripple phase, integrated and wrapped like every other clock here.
     *
     * A raw rate x wall-clock product would teleport the rings whenever Speed changed or
     * the dominant mode moved; integrating means a change bends the motion from here on,
     * which is the discipline the rest of this scene's phases already follow.
     */
    private var touchPhase = 0f

    private var idleBands = FloatArray(0)
    private var driveBands = FloatArray(0)

    private val silence = AudioFeatures.empty()

    var onShaderError: (String?) -> Unit = {}

    override fun init() {
        program = 0
        vao = 0
        uniforms = GlUtil.UniformCache(0)
        programOk = false
        plate.reset()
        drops.reset()
        touchPhase = 0f
        program =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.quad_vert),
                GlUtil.loadShader(context, R.raw.cymatics_field_frag),
            ) {
                onShaderError("Cymatics unavailable on this GPU: $it")
            }
        if (program == 0) return
        programOk = true
        uniforms = GlUtil.UniformCache(program)
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]
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

    override fun draw(timeSeconds: Float) {
        if (!programOk) return
        GlUtil.resetFrameState()
        val p = params
        val dt = lastDt.coerceIn(0f, 1f / 15f)
        val f = pendingFeatures ?: silence
        pendingFeatures = null

        plate.excite(
            bands = driveSpectrum(f, dt),
            dt = dt,
            fundamentalHz = p.cymaticsFundamental,
            drive = DRIVE_GAIN * CymaticsMath.safeDrive(p.audioDrive) * (1f + PCM_STRIKE_GAIN * pcmStrike),
            ringSeconds = CymaticsMath.ringSeconds(p.cymaticsRing),
            focus = p.cymaticsFocus,
        )
        plate.advancePhases(dt, p.speed)
        modeCount = plate.snapshot(minOf(p.cymaticsModes, style.modeCap), modes)

        var totalAmplitude = 0f
        for (i in 0 until modeCount) totalAmplitude += modes[i * 4 + 2]

        beatPulse = maxOf(LiveSignal.hit(f) * p.beatResponse.coerceIn(0f, 2f), beatPulse - dt * 3f).coerceIn(0f, 1.5f)

        val speed = p.speed.coerceIn(0.05f, 4f)
        val swirlRate = (p.cymaticsSwirl * style.swirl).coerceIn(-1f, 1f) * speed
        swirlPhase = CymaticsMath.wrapPhase(swirlPhase + swirlRate * dt, TWO_PI)
        val flowRate = (p.cymaticsFlow * style.flow).coerceIn(0f, 1f) * speed
        travelPhase = CymaticsMath.wrapPhase(travelPhase + flowRate * TRAVEL_OMEGA * dt, TWO_PI)
        driftShift = CymaticsMath.wrapPhase(driftShift + flowRate * DRIFT_RATE * dt, DRIFT_WRAP)

        // The driver rings at the loudest mode's own frequency, so the finger's ripples
        // keep time with the plate rather than beating against it.
        val touchK = (PI.toFloat() * plate.dominantWavenumber).coerceIn(MIN_TOUCH_K, MAX_TOUCH_K)
        touchPhase =
            CymaticsMath.wrapPhase(
                touchPhase + CymaticsMath.vibrationHz(plate.dominantWavenumber) * speed * TWO_PI * dt,
                TWO_PI,
            )

        if (style.shaderStyle == STYLE_FARADAY) drops.update(dt, LiveSignal.hit(f))

        // The hue nudge used to chase the chromagram's strongest pitch class: that needs a
        // track the analyser has already been through, it says nothing on live input, and it
        // steps a twelfth of the wheel between neighbouring notes. Spectral brightness is the
        // live reading of the same thing and moves continuously — a dark passage sits at one
        // end of the nudge, a bright one at the other.
        toneHue = CymaticsMath.approachHue(toneHue, LiveSignal.brightness(f), CymaticsMath.smoothing(dt, TONE_TAU_SECONDS))
        val toneNudge = sin(toneHue * TWO_PI) * TONE_HUE_SPAN

        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glUseProgram(program)
        GLES30.glUniform2f(loc("uResolution"), width.toFloat(), height.toFloat())
        GLES30.glUniform1f(loc("uTime"), time)
        GLES30.glUniform1i(loc("uStyle"), style.shaderStyle)
        GLES30.glUniform4fv(loc("uModes"), uniforms.arrayCount("uModes", CymaticsMath.MAX_RENDERED_MODES), modes, 0)
        GLES30.glUniform1i(loc("uModeCount"), modeCount)
        val geometry = style.geometryOverride ?: p.cymaticsGeometry
        GLES30.glUniform1f(loc("uGeometry"), if (geometry == 1) 1f else 0f)
        GLES30.glUniform1f(loc("uScale"), (p.cymaticsScale * style.scale).coerceIn(0.5f, 8f))
        GLES30.glUniform1f(loc("uHeightNorm"), 1f / max(totalAmplitude, MIN_COLOR_AMPLITUDE))
        GLES30.glUniform1f(loc("uFieldLive"), CymaticsMath.fieldLiveness(totalAmplitude))
        GLES30.glUniform1f(loc("uLine"), (p.cymaticsLine * style.line).coerceIn(0f, 2f))
        GLES30.glUniform1f(loc("uGlow"), (p.cymaticsGlow * style.glow).coerceIn(0f, 2f))
        GLES30.glUniform1f(loc("uFill"), (p.cymaticsFill * style.fill).coerceIn(0f, 1f))
        GLES30.glUniform1f(loc("uIridescence"), (p.cymaticsIridescence * style.iridescence).coerceIn(0f, 1f))
        GLES30.glUniform1f(loc("uCaustic"), (p.cymaticsCaustic * style.caustic).coerceIn(0f, 1.5f))
        GLES30.glUniform1f(loc("uSwirlPhase"), swirlPhase)
        GLES30.glUniform1f(loc("uTravelPhase"), travelPhase)
        GLES30.glUniform1f(loc("uDriftShift"), driftShift)
        GLES30.glUniform4fv(loc("uDrops"), uniforms.arrayCount("uDrops", CymaticsDrops.SLOTS), drops.packed, 0)
        GLES30.glUniform1f(loc("uBaseHue"), FluidHue.base(p.paletteBase) + style.hueOffset + toneNudge)
        GLES30.glUniform1f(loc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange) * style.hueSpan)
        GLES30.glUniform1f(loc("uEnergy"), f.rms.coerceIn(0f, 1.5f))
        GLES30.glUniform1f(loc("uTreble"), f.treble.coerceIn(0f, 1.5f))
        GLES30.glUniform1f(loc("uBeat"), beatPulse)
        GLES30.glUniform1f(loc("uExposure"), EXPOSURE)
        GLES30.glUniform1f(loc("uTouchK"), touchK)
        GLES30.glUniform1f(loc("uTouchPhase"), touchPhase)
        SceneTouch.upload(uniforms, touch)
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
    }

    private fun driveSpectrum(
        f: AudioFeatures,
        dt: Float,
    ): FloatArray {
        val silent = f.rms < IDLE_RMS
        val step = if (IDLE_FADE_SECONDS > 0f) dt / IDLE_FADE_SECONDS else 1f
        idleBlend = (idleBlend + if (silent) step else -step * 3f).coerceIn(0f, 1f)
        if (idleBlend <= 0f) return f.bands
        val count = if (f.bands.isNotEmpty()) f.bands.size else DEFAULT_BAND_COUNT
        if (idleBands.size != count) {
            idleBands = FloatArray(count)
            driveBands = FloatArray(count)
        }
        idlePhase = (idlePhase + dt * IDLE_SWEEP_HZ) % 1f
        val center = (0.5f - 0.42f * cos(idlePhase * 2f * PI.toFloat())) * count
        for (i in idleBands.indices) {
            val d = (i - center) / 2.6f
            idleBands[i] = 0.62f * exp(-d * d)
        }
        if (idleBlend >= 1f || f.bands.isEmpty()) return idleBands
        for (i in driveBands.indices) {
            driveBands[i] = f.bands[i] * (1f - idleBlend) + idleBands[i] * idleBlend
        }
        return driveBands
    }

    private fun loc(name: String): Int = uniforms.loc(name)

    override fun release() {
        if (program != 0) GLES30.glDeleteProgram(program)
        if (vao != 0) GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        program = 0
        vao = 0
        programOk = false
        uniforms = GlUtil.UniformCache(0)
    }
}
