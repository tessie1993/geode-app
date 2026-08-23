package dev.geode.render.fluid

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlin.math.abs

internal class WaterScene(
    private val context: Context,
) : FluidSceneBase(TIME_WRAP_SECONDS) {
    override val id: String = SceneIds.WATER

    private companion object {
        const val TOUCH_RADIUS = 0.11f

        const val MAX_TOUCH_BACKLOG = 24

        const val INK_GAIN = 0.8f

        const val TIME_WRAP_SECONDS = 628.31853f
    }

    private val sim = RippleSim(context).also { it.inkEnabled = true }
    private val emitters = FluidEmitters().also { it.choreography = choreography }

    private val splats = ArrayList<FluidSim.Splat>()

    private var displayProgram = 0
    private var displayUniforms = GlUtil.UniformCache(0)
    private var displayOk = false

    override fun init() {
        quad.forget()
        sim.onShaderError = { onShaderError(it) }
        sim.inkEnabled = true
        sim.create()
        choreography.reset()
        appliedTier = -1
        lastUserQuality = -1
        autoDowngrade = 0
        displayOk = false
        if (!sim.available) {
            onShaderError("Water style unavailable: this GPU can't render half-float buffers")
            return
        }
        displayProgram =
            GlUtil.buildProgramReporting(
                GlUtil.loadShader(context, R.raw.fluid_base_vert),
                GlUtil.loadShader(context, R.raw.water_display_frag),
            ) {
                android.util.Log.w("RippleSim", "water display shader rejected by driver: $it")
                onShaderError("Water display unavailable on this GPU: $it")
            }
        displayUniforms = GlUtil.UniformCache(displayProgram)
        displayOk = displayProgram != 0
        applyQualityTier()
    }

    override fun resize(
        width: Int,
        height: Int,
    ) {
        sim.resize(width, height)
    }

    private fun gridResFor(tierIndex: Int): Int =
        when (tierIndex) {
            0 -> 512
            1 -> 448
            2 -> 384
            3 -> 288
            else -> 192
        }

    override fun onApplyQualityTier(
        index: Int,
        userChanged: Boolean,
    ) {
        sim.applyResolution(gridResFor(index))
    }

    private var idlePhase = 0f
    private var rainAccum = 0f

    override fun idleFeatures(dt: Float): AudioFeatures {
        idlePhase = (idlePhase + dt) % TIME_WRAP_SECONDS
        val t = idlePhase
        val bass = 0.16f + 0.10f * kotlin.math.sin(t * 0.6f)
        val mid = 0.13f + 0.09f * kotlin.math.sin(t * 1.0f + 1.7f)
        val treble = 0.05f + 0.04f * kotlin.math.sin(t * 1.8f + 3.1f)
        fillIdleBands(t, 0.07f)
        return idleAudioFeatures(bass, mid, treble, rms = 0.18f)
    }

    private fun queueIdleRain(dt: Float) {
        rainAccum += dt
        if (rainAccum < 0.45f) return
        rainAccum = 0f
        val x = (kotlin.random.Random.nextFloat() * 2f - 1f) * sim.aspect * 0.85f
        val y = kotlin.random.Random.nextFloat() * 2f - 1f
        val (tr, tg, tb) = FluidHue.rgb(FluidHue.base(params.paletteBase) + 0.12f * kotlin.random.Random.nextFloat(), 0.5f)
        sim.queueDrop(x, y * 0.85f, 0.05f, 0.28f * params.waterRippleStrength.coerceIn(0f, 2f), tr, tg, tb)
    }

    private val touchStrokes = java.util.concurrent.ConcurrentLinkedQueue<FloatArray>()

    fun queueTouchStroke(
        nx: Float,
        ny: Float,
        ndx: Float,
        ndy: Float,
        dt: Float,
        strength: Float,
    ) {
        if (touchStrokes.size >= MAX_TOUCH_BACKLOG) return
        touchStrokes.add(floatArrayOf(nx, ny, ndx, ndy, dt, strength))
    }

    private fun drainTouchStrokes(
        rippleStrength: Float,
        baseHue: Float,
        p: SceneParams,
    ) {
        while (true) {
            val st = touchStrokes.poll() ?: return
            val (tr, tg, tb) = FluidHue.rgb(baseHue + 0.5f * FluidHue.range(p.hueRange), 1f)
            sim.queueStroke(
                st[0] * sim.aspect,
                st[1],
                st[2] * sim.aspect,
                st[3],
                st[4],
                TOUCH_RADIUS,
                st[5] * rippleStrength.coerceAtLeast(0.2f),
                tr,
                tg,
                tb,
            )
        }
    }

    override fun draw(timeSeconds: Float) {
        if (!sim.available || !displayOk) return
        GlUtil.resetFrameState()
        val p = params
        val f = scaledFeatures()
        val idle = isIdle

        saveGlState()

        autoQualityTick()

        sim.waveSpeed = 1.2f * p.waterWaveSpeed.coerceIn(0.2f, 2f)
        sim.damping = p.waterDamping.coerceIn(0.9f, 0.999f)
        sim.inkFlow = p.waterLiquidFlow.coerceIn(0f, 4f)
        sim.inkDissipation = p.waterLiquidFade.coerceIn(0f, 2f)

        configureChoreography()

        emitters.applyParams(p)
        emitters.forceScale = p.fluidSplatForce.coerceIn(0f, 3f)

        val simDt = lastDt.coerceIn(0f, 1f / 30f)
        choreography.tick(f, simDt, sim.aspect)
        val pcmKick = pcmStrike.coerceIn(0f, 1f)
        val rippleStrength = p.waterRippleStrength.coerceIn(0f, 2f)
        val catchRadius = WaterMath.catchWellRadius(p.fluidCatchRadius)
        val baseHue = FluidHue.base(p.paletteBase)
        emitters.tick(f, simDt, sim.aspect, baseHue, FluidHue.range(p.hueRange), splats)
        for (i in splats.indices) {
            val s = splats[i]
            val speed = kotlin.math.sqrt(s.velX * s.velX + s.velY * s.velY) / FluidEmitters.BASE_SPEED
            if (WaterMath.isCatchWell(s.r, s.g, s.b)) {
                val well = WaterMath.catchWellAmplitude(speed, catchRadius, rippleStrength)
                if (abs(well) > 1e-4f) sim.queueDrop(s.curX, s.curY, catchRadius, well)
                continue
            }
            val amp = (0.06f + 0.5f * speed.coerceAtMost(2f)) * rippleStrength * (1f + pcmKick * 0.6f)
            if (amp > 1e-4f) {
                sim.queueDrop(s.curX, s.curY, s.radius * 0.6f, amp, s.r * INK_GAIN, s.g * INK_GAIN, s.b * INK_GAIN)
            }
        }
        if (idle) queueIdleRain(lastDt)
        drainTouchStrokes(rippleStrength, baseHue, p)
        sim.step(simDt)
        pendingFeatures = null

        restoreFramebufferAndViewport()
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glUseProgram(displayProgram)
        GLES30.glUniform2f(dLoc("uInvRes"), sim.texelW, sim.texelH)
        GLES30.glUniform1f(dLoc("uAspect"), sim.aspect)
        GLES30.glUniform1f(dLoc("uTime"), time)
        GLES30.glUniform1f(dLoc("uBaseHue"), baseHue)
        GLES30.glUniform1f(dLoc("uHueSpan"), FluidHue.span(p.hueRange, p.paletteRange))
        GLES30.glUniform1f(dLoc("uDepth"), p.waterDepth.coerceIn(0f, 1f))
        GLES30.glUniform1f(dLoc("uSpecular"), p.waterSpecular.coerceIn(0f, 1f))
        GLES30.glUniform1f(dLoc("uFlowDrift"), p.waterFlow.coerceIn(0f, 1f))
        GLES30.glUniform1f(dLoc("uRefract"), 0.9f)
        GLES30.glUniform1f(dLoc("uTreble"), (f.treble + pcmKick * 0.5f).coerceIn(0f, 2f))
        GLES30.glUniform1f(dLoc("uBrightness"), WaterMath.DISPLAY_BRIGHTNESS)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sim.heightTex)
        GLES30.glUniform1i(dLoc("uHeight"), 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, if (sim.inkAvailable) sim.inkTex else sim.heightTex)
        GLES30.glUniform1i(dLoc("uInk"), 1)
        GLES30.glUniform1f(dLoc("uInkAmount"), if (sim.inkAvailable) p.waterLiquid.coerceIn(0f, 1f) else 0f)
        quad.draw()
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)

        restoreBlend()
    }

    private val quad = GlUtil.FullscreenTriangle()

    private fun dLoc(name: String): Int = displayUniforms.loc(name)

    override fun release() {
        sim.release()
        if (displayProgram != 0) GLES30.glDeleteProgram(displayProgram)
        displayProgram = 0
        displayUniforms = GlUtil.UniformCache(0)
        displayOk = false
        quad.release()
        appliedTier = -1
    }
}
