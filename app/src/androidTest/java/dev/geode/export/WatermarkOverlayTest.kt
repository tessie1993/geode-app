package dev.geode.export

import android.graphics.Bitmap
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES20
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The non-Pro watermark on a real GL context: drawn over a black frame, it must mark every
 * quadrant (it is full-frame, not a corner badge) while leaving most of the picture visible.
 *
 * The rendered frame is also saved as `watermark_<w>x<h>.png` into the instrumentation's
 * additional test output, which AGP pulls into
 * `app/build/outputs/connected_android_test_additional_output/` and CI uploads with the
 * instrumented reports: a picture of exactly what free exports carry.
 */
@RunWith(AndroidJUnit4::class)
class WatermarkOverlayTest {
    private lateinit var display: EGLDisplay
    private lateinit var context: EGLContext
    private lateinit var surface: EGLSurface

    @Before
    fun setUp() {
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val version = IntArray(2)
        check(EGL14.eglInitialize(display, version, 0, version, 1))
        val attributes =
            intArrayOf(
                EGL14.EGL_RED_SIZE,
                8,
                EGL14.EGL_GREEN_SIZE,
                8,
                EGL14.EGL_BLUE_SIZE,
                8,
                EGL14.EGL_ALPHA_SIZE,
                8,
                EGL14.EGL_RENDERABLE_TYPE,
                EGL_OPENGL_ES3_BIT,
                EGL14.EGL_SURFACE_TYPE,
                EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_NONE,
            )
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        check(EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0] > 0)
        context =
            EGL14.eglCreateContext(
                display,
                configs[0],
                EGL14.EGL_NO_CONTEXT,
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE),
                0,
            )
        val size = intArrayOf(EGL14.EGL_WIDTH, MAX_W, EGL14.EGL_HEIGHT, MAX_H, EGL14.EGL_NONE)
        surface = EGL14.eglCreatePbufferSurface(display, configs[0], size, 0)
        check(EGL14.eglMakeCurrent(display, surface, surface, context))
    }

    @After
    fun tearDown() {
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        EGL14.eglDestroySurface(display, surface)
        EGL14.eglDestroyContext(display, context)
        EGL14.eglTerminate(display)
    }

    @Test
    fun landscapeFrameIsMarkedInEveryQuadrant() = assertFullFrameMark(width = 1280, height = 720)

    @Test
    fun portraitFrameIsMarkedInEveryQuadrant() = assertFullFrameMark(width = 720, height = 1280)

    private fun assertFullFrameMark(
        width: Int,
        height: Int,
    ) {
        GLES20.glViewport(0, 0, width, height)
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        val overlay = WatermarkOverlay("MuzViz Studio Pro")
        overlay.prepare(width, height)
        overlay.draw()
        val pixels = readPixels(width, height)
        overlay.release()
        save(pixels, width, height)

        val halfW = width / 2
        val halfH = height / 2
        val quadrants =
            listOf(0 to 0, halfW to 0, 0 to halfH, halfW to halfH).map { (x0, y0) ->
                litFraction(pixels, width, x0, y0, halfW, halfH)
            }
        quadrants.forEachIndexed { i, lit ->
            assertTrue("quadrant $i has no watermark (lit fraction $lit)", lit > MIN_LIT)
        }
        val overall = litFraction(pixels, width, 0, 0, width, height)
        assertTrue("watermark hides too much of the frame ($overall)", overall < MAX_LIT)
    }

    private fun readPixels(
        width: Int,
        height: Int,
    ): IntArray {
        val buffer = ByteBuffer.allocateDirect(width * height * 4).order(ByteOrder.nativeOrder())
        GLES20.glReadPixels(0, 0, width, height, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buffer)
        val bytes = ByteArray(width * height * 4).also { buffer.get(it) }
        // RGBA rows, bottom-up, into ARGB rows, top-down.
        return IntArray(width * height) { i ->
            val x = i % width
            val y = height - 1 - i / width
            val o = (y * width + x) * 4
            (0xFF shl 24) or
                ((bytes[o].toInt() and 0xFF) shl 16) or
                ((bytes[o + 1].toInt() and 0xFF) shl 8) or
                (bytes[o + 2].toInt() and 0xFF)
        }
    }

    private fun litFraction(
        pixels: IntArray,
        stride: Int,
        x0: Int,
        y0: Int,
        w: Int,
        h: Int,
    ): Float {
        var lit = 0
        for (y in y0 until y0 + h) {
            for (x in x0 until x0 + w) {
                if (pixels[y * stride + x] and 0xFF > LIT_THRESHOLD) lit++
            }
        }
        return lit / (w * h).toFloat()
    }

    private fun save(
        pixels: IntArray,
        width: Int,
        height: Int,
    ) {
        val dir =
            InstrumentationRegistry.getArguments().getString("additionalTestOutputDir")?.let(::File)
                ?: InstrumentationRegistry.getInstrumentation().targetContext.getExternalFilesDir(null)
                ?: return
        dir.mkdirs()
        val bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
        File(dir, "watermark_${width}x$height.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }

    private companion object {
        const val EGL_OPENGL_ES3_BIT = 0x40
        const val MAX_W = 1280
        const val MAX_H = 1280
        const val LIT_THRESHOLD = 24
        const val MIN_LIT = 0.005f
        const val MAX_LIT = 0.5f
    }
}
