package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.fluid.CurlFlowMath
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.SceneParams
import kotlin.math.pow

internal class TrailPass {
    private val trail = RenderTarget("trail")
    private var fadeProgram = 0
    private var trailWarpProgram = 0
    private var fadeUniforms = GlUtil.UniformCache(0)
    private var trailUniforms = GlUtil.UniformCache(0)

    fun create(
        context: Context,
        fadeVert: String,
    ) {
        trail.forget()
        fadeProgram = GlUtil.buildProgram(fadeVert, GlUtil.loadShader(context, R.raw.fade_frag))
        trailWarpProgram = GlUtil.buildProgram(fadeVert, GlUtil.loadShader(context, R.raw.trail_warp_frag))
        fadeUniforms = GlUtil.UniformCache(fadeProgram)
        trailUniforms = GlUtil.UniformCache(trailWarpProgram)
    }

    fun apply(
        p: SceneParams,
        keep: Float,
        timeSeconds: Float,
        dt: Float,
        sceneTarget: RenderTarget,
        quadVao: Int,
        renderWidth: Int,
        renderHeight: Int,
    ) {
        if (p.trailZoom != 0f || p.trailWarp > 0f) {
            drawTrailWarp(p, keep, timeSeconds, dt, sceneTarget, quadVao, renderWidth, renderHeight)
        } else {
            drawFadeQuad(1f - (keep * 0.97f).pow(dt * 60f), quadVao)
        }
    }

    private fun drawTrailWarp(
        p: SceneParams,
        retention: Float,
        timeSeconds: Float,
        dt: Float,
        sceneTarget: RenderTarget,
        quadVao: Int,
        renderWidth: Int,
        renderHeight: Int,
    ) {
        if (!trail.ensure(renderWidth, renderHeight)) {
            drawFadeQuad(1f - (retention * 0.97f).pow(dt * 60f), quadVao)
            return
        }
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, sceneTarget.fbo)
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, trail.fbo)
        // The blit below covers the whole of trail, so last frame's copy is dead the moment it is
        // bound. trail is pure scratch - nothing samples it between the warp draw and this blit.
        trail.discardContents(GLES30.GL_DRAW_FRAMEBUFFER)
        GLES30.glBlitFramebuffer(
            0,
            0,
            renderWidth,
            renderHeight,
            0,
            0,
            renderWidth,
            renderHeight,
            GLES30.GL_COLOR_BUFFER_BIT,
            GLES30.GL_NEAREST,
        )
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, sceneTarget.fbo)
        GLES30.glViewport(0, 0, renderWidth, renderHeight)
        GLES30.glDisable(GLES30.GL_BLEND)
        // Safe only because the frame's history now lives in trail: the blit above copied it out,
        // and trail_warp writes alpha 1.0 over the full viewport with blending off, so every texel
        // of sceneTarget is rewritten before anything samples it again. The fade path below is the
        // opposite case - it blends onto these contents - which is why the discard lives here and
        // not in apply().
        sceneTarget.discardContents()
        GLES30.glUseProgram(trailWarpProgram)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, trail.tex)

        fun tLoc(n: String) = trailUniforms.loc(n)
        GLES30.glUniform1i(tLoc("uPrev"), 0)
        GLES30.glUniform1f(tLoc("uDecay"), CurlFlowMath.warpDecay(retention, dt))
        GLES30.glUniform1f(tLoc("uZoom"), p.trailZoom)
        GLES30.glUniform1f(tLoc("uWarp"), p.trailWarp)
        GLES30.glUniform1f(tLoc("uTime"), timeSeconds)
        GLES30.glBindVertexArray(quadVao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
    }

    fun drawFadeQuad(
        alpha: Float,
        quadVao: Int,
    ) {
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA)
        GLES30.glUseProgram(fadeProgram)
        GLES30.glUniform1f(fadeUniforms.loc("uFadeAlpha"), alpha.coerceIn(0.02f, 1f))
        GLES30.glBindVertexArray(quadVao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
        GLES30.glDisable(GLES30.GL_BLEND)
    }
}
