package dev.geode.store

import android.graphics.Bitmap
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES20
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.services.storage.TestStorage
import dev.geode.engine.audio.SoundTuning
import dev.geode.render.TransitionStyle
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.SceneParams
import dev.geode.thumbs.SyntheticGroove
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Renders the art the Play feature graphic is cut from: one frame per style, drawn by the real
 * native renderer at twice the graphic's 1024 x 500 (the workflow filters it down, which
 * antialiases it), with no app chrome because a feature graphic has none.
 *
 * These are renders, not screenshots, and docs/store says so: the same renderer, shaders and
 * default look the app draws, offscreen, as [dev.geode.thumbs.StyleThumbnailRenderTest] draws the
 * style thumbnails. Skipped unless `storeframes=1`. `storeFrameIds=<id,id>` picks the styles
 * (default [DEFAULT_IDS], all free ones).
 *
 * The style hears [SyntheticGroove] for [WARMUP_SECONDS] at a small size first, so its reactions
 * are mid-groove, and only the last frames are drawn at full size: a raymarched frame costs
 * seconds per megapixel on the software-rendered emulator. A style whose picture is built up over
 * frames (trails, a simulation) therefore shows only what [FULL_FRAMES] frames built; the
 * defaults are styles that draw the whole frame each time.
 */
@RunWith(AndroidJUnit4::class)
class StoreFramesTest {
    private val args = InstrumentationRegistry.getArguments()
    private lateinit var display: EGLDisplay
    private lateinit var context: EGLContext
    private lateinit var surface: EGLSurface

    @Before
    fun setUp() {
        assumeTrue("store frames render only with -e storeframes 1", args.getString("storeframes") == "1")
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        val version = IntArray(2)
        check(EGL14.eglInitialize(display, version, 0, version, 1))
        val attributes =
            intArrayOf(
                EGL14.EGL_RED_SIZE,
                COLOR_BITS,
                EGL14.EGL_GREEN_SIZE,
                COLOR_BITS,
                EGL14.EGL_BLUE_SIZE,
                COLOR_BITS,
                EGL14.EGL_ALPHA_SIZE,
                COLOR_BITS,
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
                intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, GLES_VERSION, EGL14.EGL_NONE),
                0,
            )
        val size = intArrayOf(EGL14.EGL_WIDTH, WIDTH, EGL14.EGL_HEIGHT, HEIGHT, EGL14.EGL_NONE)
        surface = EGL14.eglCreatePbufferSurface(display, configs[0], size, 0)
        check(EGL14.eglMakeCurrent(display, surface, surface, context))
    }

    @After
    fun tearDown() {
        if (!::display.isInitialized) return
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        EGL14.eglDestroySurface(display, surface)
        EGL14.eglDestroyContext(display, context)
        EGL14.eglTerminate(display)
    }

    @Test
    fun renderFeatureFrames() {
        val ids =
            args
                .getString("storeFrameIds")
                ?.split(',')
                ?.map { it.trim() }
                ?.filter { it.isNotEmpty() }
                ?: DEFAULT_IDS
        val storage = TestStorage()
        val failures = mutableListOf<String>()
        for (id in ids) {
            val started = SystemClock.elapsedRealtime()
            Log.i(TAG, "$id start")
            runCatching { renderStyle(id) }
                .onSuccess { frame ->
                    storage.openOutputFile("store-frames/$id-${WIDTH}x$HEIGHT.png").use {
                        check(frame.compress(Bitmap.CompressFormat.PNG, PNG_QUALITY, it)) { "PNG encode failed" }
                    }
                    frame.recycle()
                }.onFailure { failures += "$id: ${it.message ?: it.javaClass.simpleName}" }
            Log.i(TAG, "$id done in ${SystemClock.elapsedRealtime() - started} ms")
        }
        val report = "rendered ${ids.size - failures.size} of ${ids.size}\n" + failures.joinToString("\n")
        storage.openOutputFile("store-frames/failures.txt").use { it.write(report.toByteArray()) }
        assertTrue("no frame rendered:\n$report", failures.size < ids.size)
    }

    private fun renderStyle(id: String): Bitmap {
        val viz = NativeViz(InstrumentationRegistry.getInstrumentation().targetContext)
        try {
            check(viz.create()) { "native renderer could not be created" }
            viz.setOffscreen(true)
            viz.surfaceCreated()
            viz.surfaceChanged(WIDTH / WARMUP_DIVISOR, HEIGHT / WARMUP_DIVISOR)
            check(viz.setScene(id)) { "native renderer cannot draw it" }
            viz.setTransition(TransitionStyle.CUT.name.lowercase(), 0L)
            viz.setSoundTuning(SoundTuning())
            // The default look, as a new user first meets the style; native resolves its reaction DNA.
            viz.setParams(SceneParams(fluidAutoQuality = false))

            val groove = SyntheticGroove()
            val samplesPerFrame = groove.sampleRateHz / FPS
            val warmupFrames = WARMUP_SECONDS * FPS
            for (frame in 0 until warmupFrames + FULL_FRAMES) {
                if (frame == warmupFrames) viz.surfaceChanged(WIDTH, HEIGHT)
                val pcm = groove.next(samplesPerFrame)
                viz.writeAudio(pcm, samplesPerFrame, SyntheticGroove.CHANNELS, groove.sampleRateHz)
                viz.render(frame.toDouble() / FPS, 0)
            }
            var nativeError: String? = null
            viz.pollError { nativeError = it }
            nativeError?.let { error("native error: $it") }
            GLES20.glFinish()
            return readFrame()
        } finally {
            viz.destroy()
        }
    }

    /** The frame as an opaque bitmap, rows top-down (GL reads them bottom-up). */
    private fun readFrame(): Bitmap {
        val buffer = ByteBuffer.allocateDirect(WIDTH * HEIGHT * RGBA_BYTES).order(ByteOrder.nativeOrder())
        GLES20.glReadPixels(0, 0, WIDTH, HEIGHT, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buffer)
        val bytes = ByteArray(WIDTH * HEIGHT * RGBA_BYTES).also { buffer.get(it) }
        val pixels =
            IntArray(WIDTH * HEIGHT) { i ->
                val o = ((HEIGHT - 1 - i / WIDTH) * WIDTH + i % WIDTH) * RGBA_BYTES
                (OPAQUE shl ALPHA_SHIFT) or
                    ((bytes[o].toInt() and BYTE_MASK) shl RED_SHIFT) or
                    ((bytes[o + 1].toInt() and BYTE_MASK) shl GREEN_SHIFT) or
                    (bytes[o + 2].toInt() and BYTE_MASK)
            }
        return Bitmap.createBitmap(pixels, WIDTH, HEIGHT, Bitmap.Config.ARGB_8888).apply { setHasAlpha(false) }
    }

    private companion object {
        const val TAG = "StoreFrames"
        const val EGL_OPENGL_ES3_BIT = 0x40
        const val GLES_VERSION = 3
        const val COLOR_BITS = 8

        /** Twice Play's 1024 x 500 feature graphic. */
        const val WIDTH = 2048
        const val HEIGHT = 1000
        const val WARMUP_DIVISOR = 4
        const val WARMUP_SECONDS = 4
        const val FPS = 5
        const val FULL_FRAMES = 2
        const val PNG_QUALITY = 100
        const val RGBA_BYTES = 4
        const val OPAQUE = 0xFF
        const val BYTE_MASK = 0xFF
        const val ALPHA_SHIFT = 24
        const val RED_SHIFT = 16
        const val GREEN_SHIFT = 8

        /** Free styles that draw the whole frame each time: the first style and five family openers. */
        val DEFAULT_IDS = listOf("neon_chrome_sea", "nebula_dust", "terra_ridge", "bulb_classic", "arbor_grove", "chrys_bloom")
    }
}
