package dev.geode.export

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.opengl.GLES20
import android.opengl.GLES30
import android.opengl.GLUtils
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * The free tier's corner wordmark, composited over each exported frame.
 *
 * Drawn on the export's own EGL context straight after the scene, into the default framebuffer
 * the encoder reads, so it needs nothing from the native renderer and cannot disturb a scene: it
 * saves nothing and restores what it changes (blend, program, texture, buffer binding). The text
 * is rasterised once at [prepare] into a texture sized for the frame; per frame it is one quad.
 *
 * Sized at [HEIGHT_FRACTION] of the frame's short side and inset by [MARGIN_FRACTION], so it reads
 * the same on a 9:16 short and a 21:9 master, and sits bottom-right where players put their own
 * controls least often.
 */
class WatermarkOverlay(
    private val text: String,
) {
    private var program = 0
    private var texture = 0
    private var quad: FloatBuffer? = null
    private var aPosition = 0
    private var aTexCoord = 0
    private var uTexture = 0
    private var width = 0
    private var height = 0

    fun prepare(
        frameWidth: Int,
        frameHeight: Int,
    ) {
        width = frameWidth
        height = frameHeight
        val shortSide = minOf(frameWidth, frameHeight)
        val bitmap = rasterise(shortSide)
        texture = IntArray(1).also { GLES20.glGenTextures(1, it, 0) }[0]
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bitmap, 0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)

        // The quad in clip space, bottom-right, at the bitmap's own pixel size.
        val margin = shortSide * MARGIN_FRACTION
        val right = 1f - 2f * margin / frameWidth
        val left = right - 2f * bitmap.width / frameWidth
        val bottom = -1f + 2f * margin / frameHeight
        val top = bottom + 2f * bitmap.height / frameHeight
        bitmap.recycle()
        // x, y, u, v per corner. Bitmaps are top-down, GL textures bottom-up, hence v flipped.
        val vertices = floatArrayOf(left, bottom, 0f, 1f, right, bottom, 1f, 1f, left, top, 0f, 0f, right, top, 1f, 0f)
        quad =
            ByteBuffer
                .allocateDirect(vertices.size * Float.SIZE_BYTES)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
                .apply {
                    put(vertices)
                    position(0)
                }
        program = link(VERTEX_SHADER, FRAGMENT_SHADER)
        aPosition = GLES20.glGetAttribLocation(program, "aPosition")
        aTexCoord = GLES20.glGetAttribLocation(program, "aTexCoord")
        uTexture = GLES20.glGetUniformLocation(program, "uTexture")
    }

    /** Composites the mark over whatever is in the current framebuffer. */
    fun draw() {
        val vertices = quad ?: return
        val blendWasOn = GLES20.glIsEnabled(GLES20.GL_BLEND)
        val previousProgram = IntArray(1).also { GLES20.glGetIntegerv(GLES20.GL_CURRENT_PROGRAM, it, 0) }[0]
        // Client-side arrays are only legal with the default vertex array object bound, and the
        // native scene may have left its own VAO bound.
        GLES30.glBindVertexArray(0)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
        GLES20.glViewport(0, 0, width, height)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glDisable(GLES20.GL_SCISSOR_TEST)
        GLES20.glEnable(GLES20.GL_BLEND)
        // The bitmap is premultiplied, so ONE rather than SRC_ALPHA for the source.
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA)
        GLES20.glUseProgram(program)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture)
        GLES20.glUniform1i(uTexture, 0)
        vertices.position(0)
        GLES20.glVertexAttribPointer(aPosition, 2, GLES20.GL_FLOAT, false, STRIDE, vertices)
        GLES20.glEnableVertexAttribArray(aPosition)
        vertices.position(2)
        GLES20.glVertexAttribPointer(aTexCoord, 2, GLES20.GL_FLOAT, false, STRIDE, vertices)
        GLES20.glEnableVertexAttribArray(aTexCoord)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(aPosition)
        GLES20.glDisableVertexAttribArray(aTexCoord)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        GLES20.glUseProgram(previousProgram)
        if (!blendWasOn) GLES20.glDisable(GLES20.GL_BLEND)
    }

    fun release() {
        if (texture != 0) GLES20.glDeleteTextures(1, intArrayOf(texture), 0)
        if (program != 0) GLES20.glDeleteProgram(program)
        texture = 0
        program = 0
        quad = null
    }

    private fun rasterise(shortSide: Int): Bitmap {
        val textPx = (shortSide * HEIGHT_FRACTION).coerceAtLeast(MIN_TEXT_PX)
        val paint =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                alpha = TEXT_ALPHA
                textSize = textPx
                typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
                letterSpacing = LETTER_SPACING
                setShadowLayer(textPx * SHADOW_FRACTION, 0f, textPx * SHADOW_FRACTION / 2f, Color.argb(SHADOW_ALPHA, 0, 0, 0))
            }
        val pad = (textPx * SHADOW_FRACTION * 2f).toInt() + 2
        val width = paint.measureText(text).toInt() + pad * 2
        val metrics = paint.fontMetrics
        val height = (metrics.descent - metrics.ascent).toInt() + pad * 2
        return Bitmap.createBitmap(width.coerceAtLeast(1), height.coerceAtLeast(1), Bitmap.Config.ARGB_8888).also {
            Canvas(it).drawText(text, pad.toFloat(), pad - metrics.ascent, paint)
        }
    }

    private fun link(
        vertexSource: String,
        fragmentSource: String,
    ): Int {
        val vs = compile(GLES20.GL_VERTEX_SHADER, vertexSource)
        val fs = compile(GLES20.GL_FRAGMENT_SHADER, fragmentSource)
        return GLES20.glCreateProgram().also {
            GLES20.glAttachShader(it, vs)
            GLES20.glAttachShader(it, fs)
            GLES20.glLinkProgram(it)
            GLES20.glDeleteShader(vs)
            GLES20.glDeleteShader(fs)
        }
    }

    private fun compile(
        type: Int,
        source: String,
    ): Int =
        GLES20.glCreateShader(type).also {
            GLES20.glShaderSource(it, source)
            GLES20.glCompileShader(it)
        }

    private companion object {
        const val HEIGHT_FRACTION = 0.03f
        const val MARGIN_FRACTION = 0.025f
        const val MIN_TEXT_PX = 14f
        const val TEXT_ALPHA = 170
        const val SHADOW_ALPHA = 110
        const val SHADOW_FRACTION = 0.12f
        const val LETTER_SPACING = 0.04f
        const val STRIDE = 4 * Float.SIZE_BYTES

        const val VERTEX_SHADER =
            "attribute vec2 aPosition;\n" +
                "attribute vec2 aTexCoord;\n" +
                "varying vec2 vTexCoord;\n" +
                "void main() { vTexCoord = aTexCoord; gl_Position = vec4(aPosition, 0.0, 1.0); }\n"

        const val FRAGMENT_SHADER =
            "precision mediump float;\n" +
                "varying vec2 vTexCoord;\n" +
                "uniform sampler2D uTexture;\n" +
                "void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }\n"
    }
}
