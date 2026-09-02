package dev.geode.export

import android.content.Context
import android.opengl.GLES20
import androidx.media3.common.util.GlProgram
import androidx.media3.common.util.GlUtil
import androidx.media3.common.util.Size
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.BaseGlShaderProgram
import androidx.media3.effect.GlEffect
import androidx.media3.effect.GlShaderProgram
import dev.geode.render.TransitionCatalog
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Passes frames through untouched and leaves the last one in [store] when the clip ends. */
@UnstableApi
class TransitionCaptureEffect(
    private val store: TransitionFrameStore,
) : GlEffect {
    override fun toGlShaderProgram(
        context: Context,
        useHdr: Boolean,
    ): GlShaderProgram = TransitionCaptureProgram(store, useHdr)
}

/**
 * Opens a clip with a GL Transition from the frame in [store] to the clip's own picture, over
 * [durationUs] from the clip's first frame. Without a captured frame it is a plain pass-through.
 */
@UnstableApi
class GlTransitionEffect(
    private val def: TransitionCatalog.Def,
    private val durationUs: Long,
    private val store: TransitionFrameStore,
) : GlEffect {
    override fun toGlShaderProgram(
        context: Context,
        useHdr: Boolean,
    ): GlShaderProgram = GlTransitionProgram(def, durationUs, store, useHdr)
}

@UnstableApi
private class TransitionCaptureProgram(
    private val store: TransitionFrameStore,
    useHdr: Boolean,
) : BaseGlShaderProgram(useHdr, 1) {
    private val program = GlProgram(VERTEX_SHADER, PASSTHROUGH_FRAGMENT).also { it.bindQuad() }
    private var width = 0
    private var height = 0
    private var latest = 0

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        if (latest == 0 || width != inputWidth || height != inputHeight) {
            if (latest != 0) GlUtil.deleteTexture(latest)
            latest = GlUtil.createTexture(inputWidth, inputHeight, false)
            width = inputWidth
            height = inputHeight
        }
        return Size(inputWidth, inputHeight)
    }

    override fun drawFrame(
        inputTexId: Int,
        presentationTimeUs: Long,
    ) {
        program.use()
        program.setSamplerTexIdUniform("uTex", inputTexId, 0)
        program.bindAttributesAndUniforms()
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, QUAD_VERTICES)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, latest)
        GLES20.glCopyTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        GlUtil.checkGlError()
    }

    override fun release() {
        super.release()
        if (latest != 0) {
            store.frame = readBack(latest, width, height)
            GlUtil.deleteTexture(latest)
            latest = 0
        }
        program.delete()
    }

    private fun readBack(
        texture: Int,
        w: Int,
        h: Int,
    ): TransitionFrameStore.CapturedFrame {
        val previous = IntArray(1)
        GLES20.glGetIntegerv(GLES20.GL_FRAMEBUFFER_BINDING, previous, 0)
        val fbo = IntArray(1)
        GLES20.glGenFramebuffers(1, fbo, 0)
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, fbo[0])
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, texture, 0)
        val rgba = ByteBuffer.allocateDirect(w * h * BYTES_PER_PIXEL).order(ByteOrder.nativeOrder())
        GLES20.glReadPixels(0, 0, w, h, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, rgba)
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, previous[0])
        GLES20.glDeleteFramebuffers(1, fbo, 0)
        rgba.rewind()
        return TransitionFrameStore.CapturedFrame(w, h, rgba)
    }
}

@UnstableApi
private class GlTransitionProgram(
    private val def: TransitionCatalog.Def,
    private val durationUs: Long,
    private val store: TransitionFrameStore,
    useHdr: Boolean,
) : BaseGlShaderProgram(useHdr, 1) {
    private val blend = GlProgram(VERTEX_SHADER, fragmentFor(def)).also { it.bindQuad() }
    private val passthrough = GlProgram(VERTEX_SHADER, PASSTHROUGH_FRAGMENT).also { it.bindQuad() }
    private var from = 0
    private var ratio = 1f
    private var originUs = -1L

    override fun configure(
        inputWidth: Int,
        inputHeight: Int,
    ): Size {
        ratio = inputWidth.toFloat() / inputHeight.coerceAtLeast(1)
        val frame = store.frame
        if (from == 0 && frame != null) from = upload(frame)
        return Size(inputWidth, inputHeight)
    }

    override fun drawFrame(
        inputTexId: Int,
        presentationTimeUs: Long,
    ) {
        if (originUs < 0) originUs = presentationTimeUs
        val progress = if (durationUs <= 0) 1f else ((presentationTimeUs - originUs).toFloat() / durationUs).coerceIn(0f, 1f)
        if (from == 0 || progress >= 1f) {
            passthrough.use()
            passthrough.setSamplerTexIdUniform("uTex", inputTexId, 0)
            passthrough.bindAttributesAndUniforms()
        } else {
            blend.use()
            blend.setSamplerTexIdUniform("uFrom", from, 0)
            blend.setSamplerTexIdUniform("uTo", inputTexId, 1)
            blend.setFloatUniform("progress", progress)
            blend.setFloatUniform("ratio", ratio)
            for (param in def.params) setParam(param)
            blend.bindAttributesAndUniforms()
        }
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, QUAD_VERTICES)
        GlUtil.checkGlError()
    }

    override fun release() {
        super.release()
        if (from != 0) {
            GlUtil.deleteTexture(from)
            from = 0
        }
        blend.delete()
        passthrough.delete()
    }

    // A parameter the compiler optimised out is not an active uniform, and GlProgram refuses names it does not know.
    private fun setParam(param: TransitionCatalog.Param) {
        val v = param.values
        runCatching {
            when (param.type) {
                "float" -> blend.setFloatUniform(param.name, v.getOrElse(0) { 0f })
                "int", "bool" -> blend.setIntUniform(param.name, v.getOrElse(0) { 0f }.toInt())
                "vec2", "vec3", "vec4" -> blend.setFloatsUniform(param.name, v)
                else -> Unit
            }
        }
    }

    private fun upload(frame: TransitionFrameStore.CapturedFrame): Int {
        val texture = GlUtil.createTexture(frame.width, frame.height, false)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
        frame.rgba.rewind()
        GLES20.glTexImage2D(
            GLES20.GL_TEXTURE_2D,
            0,
            GLES20.GL_RGBA,
            frame.width,
            frame.height,
            0,
            GLES20.GL_RGBA,
            GLES20.GL_UNSIGNED_BYTE,
            frame.rgba,
        )
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        GlUtil.checkGlError()
        return texture
    }
}

@UnstableApi
private fun GlProgram.bindQuad() {
    setBufferAttribute("aFramePosition", GlUtil.getNormalizedCoordinateBounds(), GlUtil.HOMOGENEOUS_COORDINATE_VECTOR_SIZE)
}

private fun fragmentFor(def: TransitionCatalog.Def): String =
    """
    #version 300 es
    precision highp float;
    in vec2 vUv;
    out vec4 fragColor;
    uniform sampler2D uFrom;
    uniform sampler2D uTo;
    uniform float progress;
    uniform float ratio;
    vec4 getFromColor(vec2 uv) { return texture(uFrom, clamp(uv, 0.0, 1.0)); }
    vec4 getToColor(vec2 uv) { return texture(uTo, clamp(uv, 0.0, 1.0)); }
    """.trimIndent() + "\n" + def.glsl + "\nvoid main() { fragColor = transition(vUv); }\n"

private const val VERTEX_SHADER = """#version 300 es
in vec4 aFramePosition;
out vec2 vUv;
void main() {
    gl_Position = aFramePosition;
    vUv = aFramePosition.xy * 0.5 + 0.5;
}
"""

private const val PASSTHROUGH_FRAGMENT = """#version 300 es
precision mediump float;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main() { fragColor = texture(uTex, vUv); }
"""

private const val QUAD_VERTICES = 4
private const val BYTES_PER_PIXEL = 4
