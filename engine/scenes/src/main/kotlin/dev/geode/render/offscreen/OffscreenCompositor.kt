package dev.geode.render.offscreen

import android.content.Context
import android.opengl.GLES30
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.scenes.R
import dev.geode.render.BlendMode
import dev.geode.render.CompositeGrade
import dev.geode.render.fluid.CurlFlowMath
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.SceneParams
import kotlin.math.pow

internal data class OffscreenGradeUniforms(
    val enabled: Boolean,
    val zoom: Float,
    val rotation: Float,
    val saturation: Float,
    val brightness: Float,
    val contrast: Float,
    val gamma: Float,
    val hue: Float,
)

internal class OffscreenGradeState {
    var rotationAngle: Float = 0f
        private set

    var cyclePhase: Float = 0f
        private set

    var beatPulse: Float = 0f

        private set

    fun advance(
        params: SceneParams,
        dtSeconds: Float,
        impulse: Float,
    ) {
        rotationAngle = CompositeGrade.integrateRotation(rotationAngle, params.rotation, dtSeconds)
        cyclePhase = CompositeGrade.integrateCyclePhase(cyclePhase, params.cycleSpeed, dtSeconds, params.colorCycle)
        beatPulse = CompositeGrade.integrateBeatPulse(beatPulse, impulse, dtSeconds)
    }

    fun advance(
        params: SceneParams,
        dtSeconds: Float,
        beat: Boolean = false,
    ) = advance(params, dtSeconds, if (beat) 1f else 0f)

    fun pulseAmount(
        params: SceneParams,
        pulsesItself: Boolean,
    ): Float = if (pulsesItself) 0f else CompositeGrade.pulseAmount(params.pulse, beatPulse)

    fun uniforms(
        params: SceneParams,
        gradesItself: Boolean,
    ): OffscreenGradeUniforms =
        if (gradesItself) {
            OffscreenGradeUniforms(
                enabled = false,
                zoom = 1f,
                rotation = 0f,
                saturation = 1f,
                brightness = 1f,
                contrast = 1f,
                gamma = 1f,
                hue = 0f,
            )
        } else {
            OffscreenGradeUniforms(
                enabled = true,
                zoom = params.zoom,
                rotation = rotationAngle,
                saturation = params.saturation,
                brightness = CompositeGrade.brightness(params.brightness, params.intensity),
                contrast = params.contrast,
                gamma = params.gamma,
                hue = params.colorShift + cyclePhase,
            )
        }
}

internal class OffscreenCompositor(
    context: Context,
    val width: Int,
    val height: Int,
) {
    private val program = GlUtil.buildProgram(loadRaw(context, R.raw.fade_vert), loadRaw(context, R.raw.composite_frag))
    private val fadeProgram = GlUtil.buildProgram(loadRaw(context, R.raw.fade_vert), loadRaw(context, R.raw.fade_frag))
    private val trailWarpProgram = GlUtil.buildProgram(loadRaw(context, R.raw.fade_vert), loadRaw(context, R.raw.trail_warp_frag))

    private val trail = dev.geode.render.RenderTarget("offscreenTrail")

    private val flashBudget = dev.geode.render.FlashBudget()

    /**
     * The flash-rate budget. There is no unlimited path: an export is held to the same budget as
     * the screen, which is what makes a safe preview a guarantee about the file rather than a hint.
     */
    private fun flashGain(
        timeSeconds: Float,
        params: SceneParams,
        features: AudioFeatures,
    ): Float {
        val impulse =
            dev.geode.render.VisualSafety
                .flashImpulse(params.flash, dev.geode.render.LiveSignal.hit(features))
        return flashBudget.gainFor(timeSeconds, impulse)
    }

    private val noiseTex: Int = dev.geode.render.BlueNoise.createTexture(context)
    private val vao: Int

    private val sceneTarget = dev.geode.render.RenderTarget("offscreenScene")

    val sceneFbo: Int
        get() = sceneTarget.fbo
    private val emptyTex: Int

    private val grade = OffscreenGradeState()

    init {
        val ids = IntArray(1)
        GLES30.glGenVertexArrays(1, ids, 0)
        vao = ids[0]

        check(sceneTarget.ensure(width, height)) {
            "offscreen scene target ${width}x$height is incomplete on this device"
        }

        GLES30.glGenTextures(1, ids, 0)
        emptyTex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, emptyTex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D,
            0,
            GLES30.GL_RGBA8,
            1,
            1,
            0,
            GLES30.GL_RGBA,
            GLES30.GL_UNSIGNED_BYTE,
            java.nio.ByteBuffer.allocateDirect(4),
        )
    }

    fun bindSceneTarget() {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, sceneFbo)
        GLES30.glViewport(0, 0, width, height)
    }

    fun fadeSceneTargetWarp(
        params: dev.geode.render.scene.SceneParams,
        sceneFbo: Int,
        w: Int,
        h: Int,
        timeSeconds: Float,
        dtSeconds: Float = 1f / 60f,
    ) {
        if (params.trailZoom == 0f && params.trailWarp <= 0f) {
            fadeSceneTarget(params.trailLength, dtSeconds)
            return
        }
        if (!trail.ensure(w, h)) {
            fadeSceneTarget(params.trailLength, dtSeconds)
            return
        }
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, sceneFbo)
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, trail.fbo)
        GLES30.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GLES30.GL_COLOR_BUFFER_BIT, GLES30.GL_NEAREST)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, sceneFbo)
        GLES30.glViewport(0, 0, w, h)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glUseProgram(trailWarpProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, trail.tex)
        GLES30.glUniform1i(GLES30.glGetUniformLocation(trailWarpProgram, "uPrev"), 0)
        GLES30.glUniform1f(
            GLES30.glGetUniformLocation(trailWarpProgram, "uDecay"),
            CurlFlowMath.warpDecay(params.trailLength, dtSeconds),
        )
        GLES30.glUniform1f(GLES30.glGetUniformLocation(trailWarpProgram, "uZoom"), params.trailZoom)
        GLES30.glUniform1f(GLES30.glGetUniformLocation(trailWarpProgram, "uWarp"), params.trailWarp)
        GLES30.glUniform1f(GLES30.glGetUniformLocation(trailWarpProgram, "uTime"), timeSeconds)
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
    }

    fun fadeSceneTarget(
        trailLength: Float,
        dtSeconds: Float = 1f / 60f,
    ) {
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)
        GLES30.glUseProgram(fadeProgram)
        GLES30.glUniform1f(
            GLES30.glGetUniformLocation(fadeProgram, "uFadeAlpha"),
            (1f - (trailLength * 0.97f).pow(dtSeconds * 60f)).coerceIn(0.02f, 1f),
        )
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glDisable(GLES30.GL_BLEND)
    }

    fun composite(
        timeSeconds: Float,
        dtSeconds: Float,
        features: AudioFeatures,
        isShaderScene: Boolean,
        isProjectM: Boolean,
        params: SceneParams,
        flowTex: Int = 0,
        flowStrength: Float = 0f,
        rippleTex: Int = 0,
        rippleTexelW: Float = 0f,
        rippleTexelH: Float = 0f,
        rippleStrength: Float = 0f,
        rippleSpecular: Float = 0f,
        strobeHz: Float = dev.geode.render.VisualSafety.strobeHz(),
    ) {
        grade.advance(params, dtSeconds, dev.geode.render.LiveSignal.hit(features))
        val family =
            when {
                isShaderScene -> CompositeGrade.SceneFamily.SHADER
                isProjectM -> CompositeGrade.SceneFamily.MILKDROP
                else -> CompositeGrade.SceneFamily.FLUID
            }
        val gate = CompositeGrade.gateFor(family)
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        GLES30.glViewport(0, 0, width, height)
        GLES30.glDisable(GLES30.GL_BLEND)
        GLES30.glUseProgram(program)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, sceneTarget.tex)
        GLES30.glUniform1i(loc("uTexA"), 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, emptyTex)
        GLES30.glUniform1i(loc("uTexB"), 1)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE2)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, if (flowTex != 0) flowTex else emptyTex)
        GLES30.glUniform1i(loc("uFlow"), 2)
        GLES30.glUniform1f(loc("uFlowStrength"), if (flowTex != 0) flowStrength else 0f)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE4)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, noiseTex)
        GLES30.glUniform1i(loc("uNoise"), 4)
        GLES30.glUniform1f(loc("uDither"), if (noiseTex != 0) dev.geode.render.BlueNoise.DITHER_AMOUNT else 0f)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE3)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, if (rippleTex != 0) rippleTex else emptyTex)
        GLES30.glUniform1i(loc("uRipple"), 3)
        GLES30.glUniform2f(loc("uRippleTexel"), rippleTexelW, rippleTexelH)
        GLES30.glUniform1f(loc("uRippleStrength"), if (rippleTex != 0) rippleStrength else 0f)
        GLES30.glUniform1f(loc("uRippleSpecular"), if (rippleTex != 0) rippleSpecular else 0f)
        GLES30.glUniform1f(loc("uProgress"), 1f)
        GLES30.glUniform1i(loc("uStyle"), 0)
        GLES30.glUniform1f(loc("uLayerMix"), 0f)
        GLES30.glUniform1i(loc("uBlendMode"), BlendMode.NORMAL.ordinal)
        GLES30.glUniform1f(loc("uRatio"), width.toFloat() / height.toFloat())
        GLES30.glUniform1f(loc("uTime"), timeSeconds)
        GLES30.glUniform1f(loc("uBeat"), dev.geode.render.LiveSignal.hit(features))
        GLES30.glUniform1f(loc("uChroma"), params.chromaAb)
        GLES30.glUniform1f(loc("uVignette"), params.vignette)
        GLES30.glUniform1f(loc("uScanline"), params.scanlines)
        GLES30.glUniform1f(loc("uGrain"), params.grain)
        GLES30.glUniform1f(loc("uGlitch"), params.glitch)
        GLES30.glUniform1f(loc("uFisheye"), params.fisheye)
        GLES30.glUniform1f(loc("uStrobe"), params.strobe)
        GLES30.glUniform1f(loc("uStrobeHz"), strobeHz)
        GLES30.glUniform1f(loc("uPostWarp"), params.warp)
        GLES30.glUniform1f(loc("uPostRipple"), params.ripple)
        GLES30.glUniform1f(loc("uPostSymmetry"), params.symmetry.toFloat())
        GLES30.glUniform1f(loc("uPostKaleido"), if (params.kaleidoscope) 1f else 0f)
        GLES30.glUniform1f(loc("uPostPixelate"), params.pixelate)
        GLES30.glUniform1f(loc("uPostTile"), params.tile)
        GLES30.glUniform1f(loc("uPostTwist"), params.twist)
        GLES30.glUniform1f(loc("uPostBloom"), params.bloom)
        GLES30.glUniform1f(loc("uPostPosterize"), params.posterize)
        GLES30.glUniform1f(loc("uPostDriftX"), params.driftX)
        GLES30.glUniform1f(loc("uPostDriftY"), params.driftY)
        GLES30.glUniform1f(loc("uPostSway"), params.sway)
        GLES30.glUniform1f(loc("uPostShake"), params.shake)
        GLES30.glUniform1f(loc("uPostFlash"), params.flash * flashGain(timeSeconds, params, features))
        GLES30.glUniform1f(loc("uPostTemp"), params.temperature)
        GLES30.glUniform1f(loc("uPostSolarize"), if (params.solarize) 1f else 0f)
        GLES30.glUniform1f(loc("uPostMirror"), if (params.mirror) 1f else 0f)
        GLES30.glUniform1f(loc("uPostInvert"), if (params.invert) 1f else 0f)
        val gu = grade.uniforms(params, gradesItself = !gate.grade)
        GLES30.glUniform1f(loc("uPostZoom"), gu.zoom)
        GLES30.glUniform1f(loc("uPostRotation"), gu.rotation)
        GLES30.glUniform1f(loc("uPostSat"), gu.saturation)
        GLES30.glUniform1f(loc("uPostBright"), gu.brightness)
        GLES30.glUniform1f(loc("uPostContrast"), gu.contrast)
        GLES30.glUniform1f(loc("uPostGamma"), gu.gamma)
        GLES30.glUniform1f(loc("uPostHue"), gu.hue)
        GLES30.glUniform1f(loc("uPostPulse"), grade.pulseAmount(params, pulsesItself = !gate.pulse))
        val gateVec = gate.toVec4()
        GLES30.glUniform4fv(loc("uGateA"), 1, gateVec, 0)
        GLES30.glUniform4fv(loc("uGateB"), 1, gateVec, 0)
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    fun release() {
        val ids = intArrayOf(emptyTex, noiseTex)
        GLES30.glDeleteTextures(ids.size, ids, 0)
        sceneTarget.release()
        GLES30.glDeleteVertexArrays(1, intArrayOf(vao), 0)
        GLES30.glDeleteProgram(program)
        GLES30.glDeleteProgram(fadeProgram)
        GLES30.glDeleteProgram(trailWarpProgram)
        trail.release()
    }

    private val uniformLocs = HashMap<String, Int>()

    private fun loc(name: String): Int = uniformLocs.getOrPut(name) { GLES30.glGetUniformLocation(program, name) }

    private fun loadRaw(
        context: Context,
        resId: Int,
    ): String = GlUtil.loadShader(context, resId)
}
