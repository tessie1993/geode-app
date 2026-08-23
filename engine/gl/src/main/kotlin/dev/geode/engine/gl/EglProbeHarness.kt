package dev.geode.engine.gl

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.util.Log
import dev.geode.util.bestEffort
import java.util.concurrent.CancellationException

/** Where a probe context gave out. Named so a failure is diagnosable from a single log line. */
enum class EglStage {
    ALREADY_CURRENT,
    DISPLAY,
    INITIALIZE,
    CONFIG,
    CONTEXT,
    SURFACE,
    MAKE_CURRENT,
    PROBE,
}

/**
 * The result of asking for a throwaway GL context. Failure is a value, not an exception: "this
 * device would not give us a context" is an ordinary answer that puts the device on the
 * baseline, not a bug that should unwind the caller.
 */
sealed interface EglProbeOutcome<out T> {
    data class Probed<out T>(
        val value: T,
    ) : EglProbeOutcome<T>

    data class Unavailable(
        val stage: EglStage,
        val detail: String,
    ) : EglProbeOutcome<Nothing> {
        /** Short enough to sit inside [BaselineCause.NoProbeContext]'s sentence. */
        val summary: String get() = "EGL ${stage.name.lowercase()}: $detail"
    }
}

/**
 * Creates a throwaway offscreen EGL context so capabilities can be measured without the app's
 * render context — a cold-start capability read that does not sit in front of the first frame.
 *
 * **Threading contract.** [withProbeContext] makes a context current on the calling thread and
 * releases it before returning, so it must be called on a thread that has **no** current GL
 * context. Calling it on the GLSurfaceView render thread would replace that thread's context
 * mid-flight; the first thing this function does is check for that and refuse. Use
 * [GlProber.probe] directly when a context is already current.
 */
object EglProbeHarness {
    private const val TAG = "EglProbeHarness"

    /**
     * Runs [block] with a 1x1 pbuffer context current, then tears everything down.
     *
     * Notes on three decisions that are easy to get wrong here:
     *
     * - **The display is never terminated.** `EGL_DEFAULT_DISPLAY` is process-wide and shared
     *   with the app's own GLSurfaceView. `eglTerminate` on it would mark that connection's
     *   resources for destruction while the renderer is still using them. `eglInitialize` on an
     *   already-initialised display is a cheap no-op, so initialising without terminating is
     *   correct: the display is not ours to destroy.
     * - **The context asks for client version 3, not 3.1.** Requesting a minor version would
     *   fail outright on a 3.0 driver, and on a 3.1 driver would hand us a context more capable
     *   than the one the app will actually render with — `VisualizerView` and the wallpaper
     *   both ask for client version 3. A report measured on a context the app never gets is
     *   true and useless. We ask for the same thing they do and read `GL_VERSION` back.
     * - **The config carries no colour-size constraints.** Every probe renders into its own
     *   FBO, so the pbuffer's own colour depth is never used for anything; constraining it
     *   would only shrink the set of configs a device can satisfy. All that is required is that
     *   the config is ES3-renderable and can back a pbuffer.
     *
     * `Throwable` and not `Exception`: a missing EGL entry point surfaces as an `Error`, and a
     * startup probe is the last place that should be fatal. Cancellation is rethrown, which is
     * what the instance check is for.
     */
    @Suppress("TooGenericExceptionCaught", "InstanceOfCheckForException")
    fun <T> withProbeContext(block: () -> T): EglProbeOutcome<T> {
        if (EGL14.eglGetCurrentContext() != EGL14.EGL_NO_CONTEXT) {
            return EglProbeOutcome.Unavailable(
                EglStage.ALREADY_CURRENT,
                "a GL context is already current on this thread; probe it directly instead",
            )
        }

        var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
        var context: EGLContext = EGL14.EGL_NO_CONTEXT
        var surface: EGLSurface = EGL14.EGL_NO_SURFACE
        var madeCurrent = false
        return try {
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (display == EGL14.EGL_NO_DISPLAY) return failed(EglStage.DISPLAY, "no default display")

            val version = IntArray(2)
            if (!EGL14.eglInitialize(display, version, 0, version, 1)) {
                return failed(EglStage.INITIALIZE, "eglInitialize refused the default display")
            }

            val config = chooseConfig(display) ?: return failed(EglStage.CONFIG, "no ES3 pbuffer config")

            context =
                EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, CONTEXT_ATTRIBS, 0)
            if (context == EGL14.EGL_NO_CONTEXT) return failed(EglStage.CONTEXT, "eglCreateContext failed")

            surface = EGL14.eglCreatePbufferSurface(display, config, PBUFFER_ATTRIBS, 0)
            if (surface == EGL14.EGL_NO_SURFACE) return failed(EglStage.SURFACE, "eglCreatePbufferSurface failed")

            if (!EGL14.eglMakeCurrent(display, surface, surface, context)) {
                return failed(EglStage.MAKE_CURRENT, "eglMakeCurrent failed")
            }
            madeCurrent = true
            EglProbeOutcome.Probed(block())
        } catch (t: Throwable) {
            if (t is CancellationException) throw t
            Log.w(TAG, "probe context failed; the device keeps the ES 3.0 baseline", t)
            EglProbeOutcome.Unavailable(EglStage.PROBE, t.javaClass.simpleName + ": " + t.message.orEmpty())
        } finally {
            // Unwinds a half-built context just as readily as a complete one: every early
            // return above lands here with only the objects it managed to create, and each is
            // guarded by its own sentinel.
            release(display, context, surface, madeCurrent)
        }
    }

    private fun chooseConfig(display: EGLDisplay): EGLConfig? {
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        val chosen = EGL14.eglChooseConfig(display, CONFIG_ATTRIBS, 0, configs, 0, 1, count, 0)
        return if (chosen && count[0] > 0) configs[0] else null
    }

    private fun release(
        display: EGLDisplay,
        context: EGLContext,
        surface: EGLSurface,
        madeCurrent: Boolean,
    ) {
        if (display == EGL14.EGL_NO_DISPLAY) return
        bestEffort(TAG, "release the probe context") {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface)
            if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
            // Only if we actually bound something: eglReleaseThread drops this thread's EGL
            // state, which is right for the throwaway thread this is supposed to run on and
            // wrong for a thread that never got that far. eglTerminate is deliberately absent —
            // see the note on withProbeContext.
            if (madeCurrent) EGL14.eglReleaseThread()
        }
    }

    private fun failed(
        stage: EglStage,
        detail: String,
    ): EglProbeOutcome.Unavailable {
        val error = EGL14.eglGetError()
        val described = "$detail (eglGetError=0x${Integer.toHexString(error)})"
        Log.w(TAG, "probe context unavailable at ${stage.name}: $described")
        return EglProbeOutcome.Unavailable(stage, described)
    }

    private val CONFIG_ATTRIBS =
        intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE,
            EGLExt.EGL_OPENGL_ES3_BIT_KHR,
            EGL14.EGL_SURFACE_TYPE,
            EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_NONE,
        )

    private val CONTEXT_ATTRIBS = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)

    /**
     * 1x1. `EGL_KHR_surfaceless_context` would remove the surface entirely, but it is not
     * universal on the API 26 devices this app still supports, and one pixel of pbuffer costs
     * nothing next to a second code path that only some devices exercise.
     */
    private val PBUFFER_ATTRIBS =
        intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE)
}
