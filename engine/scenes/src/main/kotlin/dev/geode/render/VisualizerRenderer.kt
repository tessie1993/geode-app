package dev.geode.render

import android.content.Context
import android.content.SharedPreferences
import android.opengl.GLSurfaceView
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.NativeViz
import dev.geode.render.look.LookSnapshot
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * The GL-thread adapter over the native renderer: every frame is one `geode_viz_frame`.
 *
 * What the UI sets from other threads goes to native when it changes, never per frame.
 */
class VisualizerRenderer(
    context: Context,
) : GLSurfaceView.Renderer {
    /**
     * What this surface draws (#345): the host publishes a new [LookSnapshot] from any thread, and the parts
     * that changed (by identity) go to native at once ([NativeViz.setLook]); until one is published,
     * [LookSnapshot]'s defaults (the free first style).
     */
    @Volatile
    var look: LookSnapshot = LookSnapshot()
        set(value) {
            field = value
            nativeViz.setLook(value)
        }

    /**
     * Settings › Performance, handed to native when set. Live surfaces only: the offscreen export renderer
     * never sets it, because an export is not real time and can afford full quality. The fps cap is not
     * here; it belongs to the view's [FramePacer] (see [VisualizerView.applyPerformance]).
     */
    @Volatile
    var performance: PerformanceSettings = PerformanceSettings.HIGH
        set(value) {
            field = value
            nativeViz.setQuality(value)
        }

    /** What paces this surface, set by its host: the view's or the wallpaper's own pacer, Cast's fixed rate. */
    @Volatile
    var pacedRate: PacedRate = PacedRate { 0f }

    @Volatile
    var onShaderError: (String?) -> Unit = {}

    // Assigned from Main (EnginePlumbing.kt) and read on the GL thread in onDrawFrame.
    @Volatile
    var onMilkPresetLoaded: (String) -> Unit = {}

    /**
     * The auto-director chose a new look at a new section or a drop. Called on the GL thread, at most
     * once a frame; the app's Auto changes style on it (one Auto mode).
     */
    @Volatile
    var onDirectorStyleMoment: () -> Unit = {}

    // W02: lets the UI re-decode/re-crop the background image to the surface's new pixel size.
    // Assigned from Main (EnginePlumbing.kt), invoked on the GL thread from onSurfaceChanged.
    @Volatile
    var onSurfaceSizeChanged: (width: Int, height: Int) -> Unit = { _, _ -> }

    /**
     * The ring this surface's sound engine hears (core/sound, owned by the native renderer). Read on the
     * GL thread each frame through the renderer's own native cursor, so the engine gets every sample once,
     * in order, and is stepped with this surface's own frame time. Asked every frame, so a playback session that comes and
     * goes under a running wallpaper is picked up. Null: the engine hears nothing and its frame stays silent.
     */
    @Volatile
    var soundInput: () -> SampleRing? = { null }

    // The preset memory outlives nativeViz's handle, which VisualizerView destroys on every detach, and
    // this renderer too (native, one per process, saved here across restarts); each new handle starts on
    // the preset it remembers rather than on projectM's idle preset.
    private val appContext = context.applicationContext
    private val milkPrefs = appContext.getSharedPreferences(MILK_PREFS_FILE, Context.MODE_PRIVATE).also { seedMilkMemory(it) }

    private fun milkRestorePath(): String? = GeodeNative.milkMemoryRestorePath(appContext.assets, File(appContext.filesDir, "milk").path)

    val nativeViz =
        NativeViz(context).also { viz ->
            viz.onCreated = { milkRestorePath()?.let(viz::loadMilkPreset) }
            // The defaults above, so a handle made before the host sets anything draws them.
            viz.setLook(LookSnapshot())
            viz.setQuality(PerformanceSettings.HIGH)
        }
    private val liquidModeState = MutableStateFlow(0)

    /**
     * What Liquid mode ran last frame (`liquid::Mode`: 0 off, 1 full, 2 lite, 3 warp, 4 paused), so
     * Customize can say when a hot phone simplified or paused it. The native renderer owns the
     * answer on the GL thread and hands it over only when it changes, so a collector is never woken
     * for a label that stayed the same.
     */
    val liquidMode: StateFlow<Int> = liquidModeState.asStateFlow()

    val milkdropAvailable: Boolean
        get() {
            nativeViz.create()
            return nativeViz.knows(GeodeNative.STYLE_MILKDROP)
        }

    fun availableSceneIds(): List<String> {
        nativeViz.create()
        return nativeViz.sceneIds()
    }

    /** Frees the native renderer; only when this renderer is being discarded, on the GL thread. */
    fun releaseScenes() = nativeViz.destroy()

    /** Sheds the spare scenes before the surface pauses: the system's trim arrives when no frame reads it. GL thread. */
    fun trimSpares() = nativeViz.trimSpares()

    /** Process-wide: export, the wallpaper and the dream draw it too (core/viz/ShaderOverrides). */
    fun submitShader(
        sceneId: String,
        fragmentSrc: String,
    ) = GeodeNative.shaderOverrideSet(sceneId, fragmentSrc)

    fun customShaderFor(sceneId: String): String? = GeodeNative.shaderOverride(sceneId)

    /** Customize › Layers, process-wide: exports, the wallpaper and the dream draw it too (core/viz/LayerState). */
    fun submitLayer(
        sceneId: String?,
        mix: Float,
        blend: BlendMode,
    ) = GeodeNative.layerSet(sceneId.orEmpty(), mix, blend.ordinal)

    fun submitFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) {
        nativeViz.create()
        nativeViz.setFluidInjectionShaders(force, dye)
    }

    /**
     * When no handle exists yet, [NativeViz.create] first queues the remembered preset
     * through `onCreated`, and the load below then queues [path]. Native keeps one pending request
     * (`Renderer::loadMilkPreset`) and only the GL frame consumes it, so the second replaces the
     * first: the scene never loads the older preset, nor this one twice.
     */
    fun loadMilkPreset(path: String) {
        GeodeNative.milkMemoryRequest(path)
        nativeViz.create()
        nativeViz.loadMilkPreset(path)
    }

    fun reloadCurrentMilkPreset() = nativeViz.reloadMilkPreset()

    fun warmTransition(id: String) = nativeViz.warmTransition(id)

    /** The pointers that are down right now, in y-up NDC; `n = 0` says every finger has lifted. */
    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) = nativeViz.submitTouchPoints(xy, n)

    fun beginParamMorph(seconds: Float) {
        if (seconds <= 0f) return
        nativeViz.beginParamMorph(seconds)
    }

    override fun onSurfaceCreated(
        gl: GL10?,
        config: EGLConfig?,
    ) {
        // Native keeps the look across a new surface, starts the sound engine's cursor over and attaches the
        // platform's thermal listener once for the process.
        nativeViz.surfaceCreated()
    }

    override fun onSurfaceChanged(
        gl: GL10?,
        width: Int,
        height: Int,
    ) {
        surfaceWidth = width
        surfaceHeight = height
        nativeViz.surfaceChanged(width, height)
        onSurfaceSizeChanged(width, height)
    }

    /** The surface in pixels, 0 before the first one: the stage Record's clock is made for. */
    @Volatile
    var surfaceWidth = 0
        private set

    @Volatile
    var surfaceHeight = 0
        private set

    /** GL thread. Record's tee into [session]'s encoder; see [GeodeNative.recordSessionTeeStart]. */
    fun recordStart(
        session: Long,
        watermark: Long,
    ): Boolean = nativeViz.recordStart(session, watermark)

    fun recordStop() = nativeViz.recordStop()

    /** See [VisualizerView.setUiBlurRegion] for the contract; [NativeViz] keeps it for a new handle. */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) = nativeViz.setUiBlurRegion(left, top, right, bottom, strength, cornerPx)

    /** The wallpaper's edge glow options (`core/viz/frame/EdgeGlowPass`); any thread, kept for a new handle. */
    fun setEdgeGlow(options: FloatArray) = nativeViz.setEdgeGlow(options)

    /**
     * The whole frame is one native call (`core/viz/frame/LiveFrame`): the pacer's rate, the quality, a new
     * surface's clear-only first frame (so `GLSurfaceView.surfaceChanged`, parked on the main thread until this
     * frame ends, is released at once), the thermal reading, the gyro, the scene, the sound and the render. No
     * sample crosses here: the renderer keeps its own cursor on the ring. What comes back is acted on only when
     * its bit is set.
     */
    override fun onDrawFrame(gl: GL10?) {
        val events = nativeViz.frame(soundInput(), pacedRate)
        if (events == 0 || events and GeodeNative.FRAME_CLEAR_ONLY != 0) return
        if (events and GeodeNative.FRAME_LIQUID_CHANGED != 0) liquidModeState.value = nativeViz.liquidMode()
        if (events and GeodeNative.FRAME_ERROR_CHANGED != 0) nativeViz.pollError(onShaderError)
        if (events and GeodeNative.FRAME_MILK_LOADED != 0) {
            nativeViz.takeMilkPresetLoaded()?.let { path ->
                if (GeodeNative.milkMemoryLoaded(path)) saveMilkPath(milkPrefs, path)
                onMilkPresetLoaded(path)
            }
        }
        if (events and GeodeNative.FRAME_DIRECTOR_MOMENT != 0 && nativeViz.takeDirectorStyleMoments() > 0) onDirectorStyleMoment()
        if (events and GeodeNative.FRAME_STILL_READY != 0) nativeViz.deliverStill()
    }

    fun exportSceneFactory(sceneId: String): SceneFactory {
        val id = sceneId
        val preset = milkRestorePath()
        return object : SceneFactory {
            override val sceneId: String = id

            override val milkPresetPath: String? = preset
        }
    }

    companion object {
        /** `GeodePrefsFiles.VIZ`, and the key the shown preset has always been saved under, so installs keep it. */
        const val MILK_PREFS_FILE = "geode-viz"
        const val MILK_PATH_KEY = "milk_path"

        /** Hands native's preset memory the saved path; only a process's first seed counts. */
        fun seedMilkMemory(prefs: SharedPreferences) = GeodeNative.milkMemorySeed(prefs.getString(MILK_PATH_KEY, null))

        /** Saves the shown preset, or removes it when [path] is null. */
        fun saveMilkPath(
            prefs: SharedPreferences,
            path: String?,
        ) {
            val edit = prefs.edit()
            if (path == null) edit.remove(MILK_PATH_KEY) else edit.putString(MILK_PATH_KEY, path)
            edit.apply()
        }
    }
}
