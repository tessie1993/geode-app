package dev.geode.thumbs

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
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.TransitionStyle
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.StyleCatalogue
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * Renders the pre-baked style thumbnails that ship in `app/src/main/assets/thumbs/`.
 *
 * Not a check: a tool that happens to need a GL context, the real native renderer and the real
 * shader assets, which an instrumented test on the CI emulator has and a host build does not.
 * It is skipped unless the instrumentation argument `thumbs=1` is set, so the ordinary emulator
 * job never pays for it; `.github/workflows/render-thumbs.yml` sets it, runs only this class and
 * turns the output into a bot PR.
 *
 * Each style gets a fresh [NativeViz], prepared the way [dev.geode.render.offscreen.OffscreenSceneRenderer]
 * prepares an export (offscreen tier, cut transition, the style's own reaction DNA from
 * resolved natively), hears [SyntheticGroove] frame by frame and runs [WARMUP_SECONDS] of
 * frames before the one that is kept, so simulations have grown and reactions are mid-groove
 * rather than at their silent rest pose. NativeViz is driven directly rather than through
 * OffscreenSceneRenderer because that one only hears a decoded track (TrackAudio), and the
 * groove is generated in code.
 *
 * The frame is drawn at [RENDER_SIZE] and filtered down to [THUMB_SIZE], which antialiases the
 * thin lines many styles draw, then saved as lossy WebP through [TestStorage]: files written
 * there survive Orchestrator's clearPackageData and AGP pulls them into
 * `connected_android_test_additional_output/`. One style failing (no renderer, a native error,
 * an exception) is recorded in `thumbs/failures.txt` and the rest still render; the test fails
 * only when more than [MAX_FAILED_FRACTION] of them did, which means the renderer, not a style,
 * is broken. A frame that is one flat colour is still saved but listed as suspect.
 *
 * Arguments: `thumbsOnly=<id,id>` renders a subset; `thumbsSkip=<id,id>` leaves styles out;
 * `thumbsFps=<n>` sets the warm-up frame rate (default [DEFAULT_WARMUP_FPS]; the emulator renders
 * in software, so frames are the cost) and `thumbsMarchFps=<n>` the rate for the raymarched styles
 * ([ParamScope.MARCH_DETAIL], default [DEFAULT_MARCH_WARMUP_FPS]). A marched frame costs seconds
 * on the emulator (vanishing took 228-311 s at 15 fps in #331's runs) and there are 88 of them,
 * which at 15 fps alone outlasts the workflow's timeout; they are stateless raymarches, so fewer
 * warm-up frames still land mid-groove.
 *
 * Each style logs `<id> start` before it renders, so when a style takes the emulator down (#331:
 * both runs died on the style after vanishing) logcat names it; the workflow pulls the device
 * folder while the test runs, so the thumbnails already written survive such a crash.
 *
 * MilkDrop is skipped: its picture is whichever preset is loaded, so the picker uses its own art.
 */
@RunWith(AndroidJUnit4::class)
class StyleThumbnailRenderTest {
    private val args = InstrumentationRegistry.getArguments()
    private lateinit var display: EGLDisplay
    private lateinit var context: EGLContext
    private lateinit var surface: EGLSurface

    @Before
    fun setUp() {
        assumeTrue("thumbnail render runs only with -e thumbs 1", args.getString("thumbs") == "1")
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
        val size = intArrayOf(EGL14.EGL_WIDTH, RENDER_SIZE, EGL14.EGL_HEIGHT, RENDER_SIZE, EGL14.EGL_NONE)
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
    fun renderEveryStyle() {
        val only =
            args
                .getString("thumbsOnly")
                ?.split(',')
                ?.map { it.trim() }
                ?.filter { it.isNotEmpty() }
                ?.toSet()
        val fps =
            args
                .getString("thumbsFps")
                ?.toIntOrNull()
                ?.coerceIn(MIN_WARMUP_FPS, MAX_WARMUP_FPS)
                ?: DEFAULT_WARMUP_FPS
        val marchFps =
            args
                .getString("thumbsMarchFps")
                ?.toIntOrNull()
                ?.coerceIn(MIN_WARMUP_FPS, MAX_WARMUP_FPS)
                ?: minOf(fps, DEFAULT_MARCH_WARMUP_FPS)
        val skip =
            args
                .getString("thumbsSkip")
                ?.split(',')
                ?.map { it.trim() }
                ?.filter { it.isNotEmpty() }
                ?.toSet()
                .orEmpty()
        val ids =
            (StyleCatalogue.ids - GeodeNative.STYLE_MILKDROP)
                .filter { only == null || it in only }
                .filter { it !in skip }
        assertTrue("no styles to render (thumbsOnly=$only)", ids.isNotEmpty())

        val storage = TestStorage()
        val failures = mutableListOf<String>()
        val suspects = mutableListOf<String>()
        for (id in ids) {
            val started = SystemClock.elapsedRealtime()
            Log.i(TAG, "$id start")
            val styleFps = if (ParamScope.MARCH_DETAIL.appliesTo(id)) marchFps else fps
            val result = runCatching { renderStyle(id, styleFps) }
            result
                .onSuccess { pixels ->
                    if (isFlat(pixels)) suspects += "$id: one flat colour"
                    storage.openOutputFile("thumbs/$id.webp").use { it.write(encode(pixels)) }
                }.onFailure { failures += "$id: ${it.message ?: it.javaClass.simpleName}" }
            Log.i(TAG, "$id ${if (result.isSuccess) "ok" else "FAILED"} in ${SystemClock.elapsedRealtime() - started} ms")
        }

        val report =
            buildString {
                appendLine("rendered ${ids.size - failures.size} of ${ids.size} styles at $fps fps warm-up ($marchFps fps marched)")
                if (skip.isNotEmpty()) appendLine("skipped on request: ${skip.sorted().joinToString()}")
                appendLine("failed (${failures.size}):")
                failures.forEach { appendLine("  $it") }
                appendLine("suspect, still saved (${suspects.size}):")
                suspects.forEach { appendLine("  $it") }
            }
        storage.openOutputFile("thumbs/failures.txt").use { it.write(report.toByteArray()) }
        Log.i(TAG, report)
        assertTrue(
            "too many styles failed to render:\n$report",
            failures.size <= ids.size * MAX_FAILED_FRACTION,
        )
    }

    /** One style's kept frame, ARGB rows top-down at [RENDER_SIZE] square. */
    private fun renderStyle(
        id: String,
        fps: Int,
    ): IntArray {
        val viz = NativeViz(InstrumentationRegistry.getInstrumentation().targetContext)
        try {
            check(viz.create()) { "native renderer could not be created" }
            viz.setOffscreen(true)
            viz.surfaceCreated()
            viz.surfaceChanged(RENDER_SIZE, RENDER_SIZE)
            check(viz.setScene(id)) { "native renderer cannot draw it" }
            viz.setTransition(TransitionStyle.CUT.name.lowercase(), 0L)
            viz.setSoundTuning(null)
            // Default Customize values: the thumb shows the style as a new user first meets it.
            val params = SceneParams(fluidAutoQuality = false)
            // No user routes: native resolves the style's DNA for this scene and reactSeed before the first render.
            viz.setParams(params)

            val groove = SyntheticGroove()
            val samplesPerFrame = groove.sampleRateHz / fps
            val frames = WARMUP_SECONDS * fps + 1
            for (frame in 0 until frames) {
                val pcm = groove.next(samplesPerFrame)
                viz.writeAudio(pcm, samplesPerFrame, SyntheticGroove.CHANNELS, groove.sampleRateHz)
                viz.render(frame.toDouble() / fps, 0)
            }
            var nativeError: String? = null
            viz.pollError { nativeError = it }
            nativeError?.let { error("native error: $it") }
            GLES20.glFinish()
            return readPixels()
        } finally {
            viz.destroy()
        }
    }

    private fun readPixels(): IntArray {
        val n = RENDER_SIZE
        val buffer = ByteBuffer.allocateDirect(n * n * 4).order(ByteOrder.nativeOrder())
        GLES20.glReadPixels(0, 0, n, n, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buffer)
        val bytes = ByteArray(n * n * 4).also { buffer.get(it) }
        // RGBA rows, bottom-up, into opaque ARGB rows, top-down. Alpha is forced: some scenes
        // leave it below 1 and the picker draws thumbs on glass, where that would show through.
        return IntArray(n * n) { i ->
            val o = ((n - 1 - i / n) * n + i % n) * 4
            (0xFF shl 24) or
                ((bytes[o].toInt() and 0xFF) shl 16) or
                ((bytes[o + 1].toInt() and 0xFF) shl 8) or
                (bytes[o + 2].toInt() and 0xFF)
        }
    }

    private fun encode(pixels: IntArray): ByteArray {
        val full = Bitmap.createBitmap(pixels, RENDER_SIZE, RENDER_SIZE, Bitmap.Config.ARGB_8888)
        val thumb = Bitmap.createScaledBitmap(full, THUMB_SIZE, THUMB_SIZE, true)
        val bytes =
            ByteArrayOutputStream().use { out ->
                check(thumb.compress(Bitmap.CompressFormat.WEBP_LOSSY, WEBP_QUALITY, out)) { "WebP encode failed" }
                out.toByteArray()
            }
        full.recycle()
        thumb.recycle()
        return bytes
    }

    // A frame whose every pixel is within a few levels of the first: black, or a cleared target.
    private fun isFlat(pixels: IntArray): Boolean {
        val first = pixels[0]
        return pixels.all { p ->
            (0 until 3).all { c ->
                val shift = c * 8
                abs((p shr shift and 0xFF) - (first shr shift and 0xFF)) <= FLAT_TOLERANCE
            }
        }
    }

    private companion object {
        const val TAG = "StyleThumbnailRender"
        const val EGL_OPENGL_ES3_BIT = 0x40
        const val RENDER_SIZE = 512
        const val THUMB_SIZE = 256
        const val WEBP_QUALITY = 75
        const val WARMUP_SECONDS = 4
        const val DEFAULT_WARMUP_FPS = 15
        const val DEFAULT_MARCH_WARMUP_FPS = 5
        const val MIN_WARMUP_FPS = 5
        const val MAX_WARMUP_FPS = 60
        const val MAX_FAILED_FRACTION = 0.1
        const val FLAT_TOLERANCE = 3
    }
}
