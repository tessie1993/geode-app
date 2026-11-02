package dev.geode.wallpaper

import android.opengl.GLES30
import android.opengl.GLSurfaceView
import dev.geode.RingLog
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundInput
import dev.geode.render.VisualizerRenderer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.sqrt

/**
 * The wallpaper's GL renderer: the visual, then, in [WallpaperLook.EDGE], the edge glow over it.
 *
 * A wrapper rather than a change to [VisualizerRenderer], which every host shares: the glow is a
 * wallpaper look, and drawing it after the visual's composite (which targets FBO 0) and before
 * GLSurfaceView swaps keeps the visual's own passes untouched.
 */
class WallpaperRenderer(
    private val visual: VisualizerRenderer,
    /** The sound the glow breathes with; the same input the visual hears. */
    private val sound: () -> SoundInput?,
) : GLSurfaceView.Renderer {
    /** Set on the main thread, read once per frame on the GL thread. */
    @Volatile var edge: Boolean = false

    private val glow = EdgeGlow()

    /** The part of the surface on screen, in surface pixels; see [setVisibleWindow]. */
    @Volatile private var window = floatArrayOf(0f, 0f, 1f, 1f)

    /**
     * A wallpaper surface can be wider than the display so the launcher can pan it; the edge
     * belongs on the screen's edge, not on the surface's, which is off screen on middle pages.
     */
    fun setVisibleWindow(
        x: Float,
        y: Float,
        width: Float,
        height: Float,
    ) {
        window = floatArrayOf(x, y, width, height)
    }

    override fun onSurfaceCreated(
        gl: GL10?,
        config: EGLConfig?,
    ) {
        visual.onSurfaceCreated(gl, config)
        glow.onContextCreated()
    }

    override fun onSurfaceChanged(
        gl: GL10?,
        width: Int,
        height: Int,
    ) {
        visual.onSurfaceChanged(gl, width, height)
        glow.onSurfaceChanged(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        visual.onDrawFrame(gl)
        if (edge) glow.draw(window, sound())
    }
}

/**
 * The edge look: the visual stays along the screen's rounded rim and fades to black inward.
 *
 * The black is real black, so on an OLED panel most of the screen is switched off, and icons and
 * the lock clock sit on black instead of on a busy picture. The band breathes with the sound's
 * level, read straight off the ring each frame (the newest [LEVEL_FRAMES], attack fast and
 * release slow, the way a VU meter moves), so it pulses on the beat without the renderer's
 * frame data crossing the JNI line.
 *
 * GL thread only. One attribute-less triangle covers the screen; the shader is a rounded-rect
 * distance field. The GL state it changes is saved and put back, so the visual's next frame
 * starts from exactly the state it left.
 */
internal class EdgeGlow {
    private var program = 0
    private var failed = false
    private var uWindow = -1
    private var uWidth = -1
    private var uRadius = -1
    private var surfaceWidth = 1
    private var surfaceHeight = 1

    private val levelWindow = Array(1) { FloatArray(LEVEL_FRAMES) }
    private var heardRing: SampleRing? = null
    private var heardWritten = -1L
    private var level = 0f

    private val saved = IntArray(SAVED_INTS)

    fun onContextCreated() {
        // A new context has none of the old objects; the old id must not be deleted or reused.
        program = 0
        failed = false
    }

    fun onSurfaceChanged(
        width: Int,
        height: Int,
    ) {
        surfaceWidth = width.coerceAtLeast(1)
        surfaceHeight = height.coerceAtLeast(1)
    }

    fun draw(
        window: FloatArray,
        input: SoundInput?,
    ) {
        if (failed) return
        if (program == 0 && !build()) return
        updateLevel(input)
        val shortSide = minOf(window[2], window[3])
        val band = shortSide * (BAND_MIN + BAND_SWING * level)
        val radius = shortSide * CORNER_FRACTION

        saveState()
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        GLES30.glViewport(0, 0, surfaceWidth, surfaceHeight)
        GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        GLES30.glDisable(GLES30.GL_STENCIL_TEST)
        GLES30.glDisable(GLES30.GL_SCISSOR_TEST)
        GLES30.glDisable(GLES30.GL_CULL_FACE)
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendEquation(GLES30.GL_FUNC_ADD)
        // Darken colour only; the surface's alpha stays what the visual wrote.
        GLES30.glBlendFuncSeparate(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA, GLES30.GL_ZERO, GLES30.GL_ONE)
        GLES30.glUseProgram(program)
        // gl_FragCoord is bottom-up; the window is top-down surface pixels.
        GLES30.glUniform4f(uWindow, window[0], surfaceHeight - window[1] - window[3], window[2], window[3])
        GLES30.glUniform1f(uWidth, band)
        GLES30.glUniform1f(uRadius, radius)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        restoreState()
    }

    private fun updateLevel(input: SoundInput?) {
        val ring = input?.ring
        var target = 0f
        if (ring != null) {
            val written = ring.writtenFrames
            val fresh = ring !== heardRing || written != heardWritten
            heardRing = ring
            heardWritten = written
            if (fresh && ring.snapshotLatest(levelWindow)) {
                var sum = 0f
                for (s in levelWindow[0]) sum += s * s
                target = (sqrt(sum / LEVEL_FRAMES) * LEVEL_GAIN).coerceIn(0f, 1f)
            } else if (!fresh) {
                // Nothing new since the last frame (paused, between captures): ease off.
                target = level * HOLD_DECAY
            }
        }
        level += (target - level) * if (target > level) ATTACK else RELEASE
    }

    private fun build(): Boolean {
        val vs = compile(GLES30.GL_VERTEX_SHADER, VERTEX)
        val fs = compile(GLES30.GL_FRAGMENT_SHADER, FRAGMENT)
        if (vs == 0 || fs == 0) {
            if (vs != 0) GLES30.glDeleteShader(vs)
            if (fs != 0) GLES30.glDeleteShader(fs)
            return fail("edge glow shader did not compile")
        }
        val p = GLES30.glCreateProgram()
        GLES30.glAttachShader(p, vs)
        GLES30.glAttachShader(p, fs)
        GLES30.glLinkProgram(p)
        GLES30.glDeleteShader(vs)
        GLES30.glDeleteShader(fs)
        val linked = IntArray(1)
        GLES30.glGetProgramiv(p, GLES30.GL_LINK_STATUS, linked, 0)
        if (linked[0] == 0) {
            RingLog.note(TAG, "edge glow link: ${GLES30.glGetProgramInfoLog(p)}")
            GLES30.glDeleteProgram(p)
            return fail("edge glow program did not link")
        }
        program = p
        uWindow = GLES30.glGetUniformLocation(p, "uWindow")
        uWidth = GLES30.glGetUniformLocation(p, "uWidth")
        uRadius = GLES30.glGetUniformLocation(p, "uRadius")
        return true
    }

    private fun compile(
        type: Int,
        source: String,
    ): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, source)
        GLES30.glCompileShader(shader)
        val ok = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, ok, 0)
        if (ok[0] != 0) return shader
        RingLog.note(TAG, "edge glow compile: ${GLES30.glGetShaderInfoLog(shader)}")
        GLES30.glDeleteShader(shader)
        return 0
    }

    /** A broken driver shows the full visual rather than retrying a compile every frame. */
    private fun fail(why: String): Boolean {
        RingLog.note(TAG, why)
        failed = true
        return false
    }

    private fun saveState() {
        GLES30.glGetIntegerv(GLES30.GL_CURRENT_PROGRAM, saved, I_PROGRAM)
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, saved, I_FBO)
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, saved, I_VIEWPORT)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_SRC_RGB, saved, I_SRC_RGB)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_DST_RGB, saved, I_DST_RGB)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_SRC_ALPHA, saved, I_SRC_A)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_DST_ALPHA, saved, I_DST_A)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_EQUATION_RGB, saved, I_EQ_RGB)
        GLES30.glGetIntegerv(GLES30.GL_BLEND_EQUATION_ALPHA, saved, I_EQ_A)
        for ((i, cap) in CAPS.withIndex()) saved[I_CAPS + i] = if (GLES30.glIsEnabled(cap)) 1 else 0
    }

    private fun restoreState() {
        for ((i, cap) in CAPS.withIndex()) {
            if (saved[I_CAPS + i] != 0) GLES30.glEnable(cap) else GLES30.glDisable(cap)
        }
        GLES30.glBlendEquationSeparate(saved[I_EQ_RGB], saved[I_EQ_A])
        GLES30.glBlendFuncSeparate(saved[I_SRC_RGB], saved[I_DST_RGB], saved[I_SRC_A], saved[I_DST_A])
        GLES30.glViewport(saved[I_VIEWPORT], saved[I_VIEWPORT + 1], saved[I_VIEWPORT + 2], saved[I_VIEWPORT + 3])
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, saved[I_FBO])
        GLES30.glUseProgram(saved[I_PROGRAM])
    }

    private companion object {
        const val TAG = "WallpaperEdge"
        const val LEVEL_FRAMES = 512

        /** The band at rest and at full level, as fractions of the screen's short side. */
        const val BAND_MIN = 0.045f
        const val BAND_SWING = 0.12f

        /** Close to a modern phone's display corners; the black hides any small mismatch. */
        const val CORNER_FRACTION = 0.09f
        const val LEVEL_GAIN = 3.5f
        const val ATTACK = 0.5f
        const val RELEASE = 0.08f
        const val HOLD_DECAY = 0.9f

        val CAPS =
            intArrayOf(
                GLES30.GL_BLEND,
                GLES30.GL_DEPTH_TEST,
                GLES30.GL_STENCIL_TEST,
                GLES30.GL_SCISSOR_TEST,
                GLES30.GL_CULL_FACE,
            )
        const val I_PROGRAM = 0
        const val I_FBO = 1
        const val I_VIEWPORT = 2
        const val I_SRC_RGB = 6
        const val I_DST_RGB = 7
        const val I_SRC_A = 8
        const val I_DST_A = 9
        const val I_EQ_RGB = 10
        const val I_EQ_A = 11
        const val I_CAPS = 12
        const val SAVED_INTS = 17

        /** A screen-covering triangle from gl_VertexID alone: no buffers, no attributes. */
        const val VERTEX = """#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""

        /**
         * Black, weighted by how far inside the screen's rounded rectangle the pixel is: clear
         * on the rim, fully black [uWidth] pixels in, black outside the rounded corners.
         */
        const val FRAGMENT = """#version 300 es
precision highp float;
uniform vec4 uWindow;
uniform float uWidth;
uniform float uRadius;
out vec4 fragColor;
void main() {
    vec2 halfSize = uWindow.zw * 0.5;
    vec2 p = gl_FragCoord.xy - (uWindow.xy + halfSize);
    vec2 q = abs(p) - (halfSize - vec2(uRadius));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
    float inside = smoothstep(0.0, uWidth, -d);
    float a = d > 0.0 ? 1.0 : inside * inside * (3.0 - 2.0 * inside);
    fragColor = vec4(0.0, 0.0, 0.0, a);
}
"""
    }
}
