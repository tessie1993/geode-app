package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.SceneParams
import java.nio.ByteBuffer

internal class CompositePass(
    private val context: Context,
) {
    class Inputs {
        var texA = 0
        var texB = 0
        var flowTex = 0
        var flowStrength = 0f
        var rippleTex = 0
        var rippleTexelW = 0f
        var rippleTexelH = 0f
        var rippleStrength = 0f
        var rippleSpecular = 0f
        var progress = 1f
        var layerMix = 0.5f
        var blendOrdinal = 0
        var hasLayer = false
        var hasOutgoing = false
        var transitionStyle: TransitionStyle = TransitionStyle.FADE
        var transitionId: String = TransitionStyle.FADE.name.lowercase()
        var ratio = 1f
        var timeSeconds = 0f
        var hitImpulse = 0f
        var flash = 0f
        var strobeHz = 0f
        var postRotationAngle = 0f
        var postCyclePhase = 0f
        var postBeatPulse = 0f
        var quadVao = 0
        var fx: SceneParams = SceneParams.DEFAULT
        var gateA: FloatArray = FloatArray(4)
        var gateB: FloatArray = FloatArray(4)
    }

    private val transitions = TransitionPrograms(context)
    private var program = GlUtil.UniformCache(0)
    private var noiseTex = 0
    private var zeroTexId = 0

    val zeroTex: Int get() = zeroTexId

    private fun loc(name: String): Int = program.loc(name)

    fun releaseStaleTextures() {
        if (noiseTex != 0) {
            GLES30.glDeleteTextures(1, intArrayOf(noiseTex), 0)
            noiseTex = 0
        }
    }

    fun create(fadeVert: String) {
        transitions.create(fadeVert)
        noiseTex = BlueNoise.createTexture(context)
        zeroTexId = createZeroTexture()
    }

    private fun createZeroTexture(): Int {
        val texIds = IntArray(1)
        GLES30.glGenTextures(1, texIds, 0)
        val tex = texIds[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        val zero =
            ByteBuffer
                .allocateDirect(4)
                .apply { put(byteArrayOf(0, 0, 0, 0)).position(0) }
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D,
            0,
            GLES30.GL_RGBA8,
            1,
            1,
            0,
            GLES30.GL_RGBA,
            GLES30.GL_UNSIGNED_BYTE,
            zero,
        )
        return tex
    }

    fun warmTransition(id: String) = transitions.warm(id)

    fun draw(inputs: Inputs) {
        // The composite is the frame's one opaque fullscreen pass: it samples texA/texB itself and
        // owes nothing to what is already in the target. Blending on would cost the whole screen in
        // read-modify-write bandwidth for a result identical to a straight write, and it would also
        // mean the target's previous contents are live - which is what stops this pass from being a
        // discard-then-overwrite the way the offscreen passes are. Owned here rather than left to
        // the caller so the pass's own contract is provable from this file.
        GLES30.glDisable(GLES30.GL_BLEND)
        program = transitions.programFor(inputs.transitionId)
        val definition = transitions.definition(inputs.transitionId)
        GLES30.glUseProgram(program.program)
        transitions.uploadParamsIfNeeded(program, definition)

        bindTextures(inputs)
        uploadFrameUniforms(inputs, definition)
        uploadGradeUniforms(inputs.fx, inputs)

        GLES30.glUniform4fv(loc("uGateA"), 1, inputs.gateA, 0)
        GLES30.glUniform4fv(loc("uGateB"), 1, inputs.gateB, 0)
        GLES30.glBindVertexArray(inputs.quadVao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
    }

    private fun bindTextures(inputs: Inputs) {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputs.texA)
        GLES30.glUniform1i(loc("uTexA"), 0)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE1)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputs.texB)
        GLES30.glUniform1i(loc("uTexB"), 1)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE2)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputs.flowTex)
        GLES30.glUniform1i(loc("uFlow"), 2)
        GLES30.glUniform1f(loc("uFlowStrength"), inputs.flowStrength)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE3)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, inputs.rippleTex)
        GLES30.glUniform1i(loc("uRipple"), 3)
        GLES30.glUniform2f(loc("uRippleTexel"), inputs.rippleTexelW, inputs.rippleTexelH)
        GLES30.glUniform1f(loc("uRippleStrength"), inputs.rippleStrength)
        GLES30.glUniform1f(loc("uRippleSpecular"), inputs.rippleSpecular)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE4)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, noiseTex)
        GLES30.glUniform1i(loc("uNoise"), 4)
        GLES30.glUniform1f(loc("uDither"), if (noiseTex != 0) BlueNoise.DITHER_AMOUNT else 0f)
    }

    private fun uploadFrameUniforms(
        inputs: Inputs,
        definition: TransitionCatalog.Def?,
    ) {
        GLES30.glUniform1f(loc("uProgress"), inputs.progress)
        GLES30.glUniform1f(loc("uLayerMix"), inputs.layerMix)
        GLES30.glUniform1i(loc("uBlendMode"), inputs.blendOrdinal)
        val styleValue =
            when {
                inputs.hasLayer -> VisualizerRenderer.STYLE_LAYER
                !inputs.hasOutgoing -> TransitionStyle.CUT.ordinal
                definition != null -> TransitionCatalog.STYLE_LIBRARY
                else -> inputs.transitionStyle.ordinal
            }
        GLES30.glUniform1i(loc("uStyle"), styleValue)
        GLES30.glUniform1f(loc("uRatio"), inputs.ratio)
        GLES30.glUniform1f(loc("uTime"), inputs.timeSeconds)
        GLES30.glUniform1f(loc("uBeat"), inputs.hitImpulse)
    }

    private fun uploadGradeUniforms(
        fx: SceneParams,
        inputs: Inputs,
    ) {
        GLES30.glUniform1f(loc("uChroma"), fx.chromaAb)
        GLES30.glUniform1f(loc("uVignette"), fx.vignette)
        GLES30.glUniform1f(loc("uScanline"), fx.scanlines)
        GLES30.glUniform1f(loc("uGrain"), fx.grain)
        GLES30.glUniform1f(loc("uGlitch"), fx.glitch)
        GLES30.glUniform1f(loc("uFisheye"), fx.fisheye)
        GLES30.glUniform1f(loc("uStrobe"), fx.strobe)
        GLES30.glUniform1f(loc("uStrobeHz"), inputs.strobeHz)
        GLES30.glUniform1f(loc("uPostWarp"), fx.warp)
        GLES30.glUniform1f(loc("uPostRipple"), fx.ripple)
        GLES30.glUniform1f(loc("uPostSymmetry"), fx.symmetry.toFloat())
        GLES30.glUniform1f(loc("uPostKaleido"), if (fx.kaleidoscope) 1f else 0f)
        GLES30.glUniform1f(loc("uPostPixelate"), fx.pixelate)
        GLES30.glUniform1f(loc("uPostTile"), fx.tile)
        GLES30.glUniform1f(loc("uPostTwist"), fx.twist)
        GLES30.glUniform1f(loc("uPostBloom"), fx.bloom)
        GLES30.glUniform1f(loc("uPostPosterize"), fx.posterize)
        GLES30.glUniform1f(loc("uPostDriftX"), fx.driftX)
        GLES30.glUniform1f(loc("uPostDriftY"), fx.driftY)
        GLES30.glUniform1f(loc("uPostSway"), fx.sway)
        GLES30.glUniform1f(loc("uPostShake"), fx.shake)
        GLES30.glUniform1f(loc("uPostFlash"), inputs.flash)
        GLES30.glUniform1f(loc("uPostTemp"), fx.temperature)
        GLES30.glUniform1f(loc("uPostSolarize"), if (fx.solarize) 1f else 0f)
        GLES30.glUniform1f(loc("uPostMirror"), if (fx.mirror) 1f else 0f)
        GLES30.glUniform1f(loc("uPostInvert"), if (fx.invert) 1f else 0f)
        GLES30.glUniform1f(loc("uPostZoom"), fx.zoom)
        GLES30.glUniform1f(loc("uPostRotation"), inputs.postRotationAngle)
        GLES30.glUniform1f(loc("uPostSat"), fx.saturation)
        GLES30.glUniform1f(loc("uPostBright"), CompositeGrade.brightness(fx.brightness, fx.intensity))
        GLES30.glUniform1f(loc("uPostContrast"), fx.contrast)
        GLES30.glUniform1f(loc("uPostGamma"), fx.gamma)
        GLES30.glUniform1f(loc("uPostHue"), fx.colorShift + inputs.postCyclePhase)
        GLES30.glUniform1f(loc("uPostPulse"), CompositeGrade.pulseAmount(fx.pulse, inputs.postBeatPulse))
    }
}
