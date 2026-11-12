package dev.geode.render

import android.content.Context
import android.opengl.GLSurfaceView
import android.os.SystemClock
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.NativeViz
import dev.geode.render.look.LookApplier
import dev.geode.render.look.LookSnapshot
import dev.geode.render.scene.SceneIds
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.atomic.AtomicLong
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * The GL-thread adapter over the native renderer: every frame is one `geode_viz_render`.
 *
 * The `@Volatile` properties are the state the UI publishes from other threads; each frame hands
 * the current values across before rendering.
 */
class VisualizerRenderer(
    context: Context,
) : GLSurfaceView.Renderer {
    /**
     * What this surface draws (#345): the host publishes a new [LookSnapshot] from any thread, and each
     * frame [LookApplier] pushes the parts native asks for. A field that changed (by identity) is a new
     * version for native's `PushVersions`; until one is published, [LookSnapshot]'s defaults (the free first style).
     */
    @Volatile
    var look: LookSnapshot = LookSnapshot()
        set(value) {
            val was = field
            field = value
            if (value.params !== was.params) paramsVersion = pushVersions.incrementAndGet()
            if (value.lfos !== was.lfos) lfoVersion = pushVersions.incrementAndGet()
            if (value.adsrs !== was.adsrs) adsrVersion = pushVersions.incrementAndGet()
            if (value.soundTuning !== was.soundTuning) tuningVersion = pushVersions.incrementAndGet()
        }

    @Volatile private var paramsVersion = 0L

    @Volatile private var lfoVersion = 0L

    @Volatile private var adsrVersion = 0L

    @Volatile private var tuningVersion = 0L

    private val lookApplier = LookApplier()

    /**
     * Settings › Performance. Live surfaces only: the offscreen export renderer never reads it,
     * because an export is not real time and can afford full quality. The fps cap is not here;
     * it belongs to the view's [FramePacer] (see [VisualizerView.applyPerformance]).
     */
    @Volatile
    var performance: PerformanceSettings = PerformanceSettings.HIGH

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

    /**
     * [GyroLook]'s last reading, null while off; core/viz/GyroLook makes the look. [gyroSession]
     * bumps on each switch on, so native recaptures even if no frame saw it off.
     */
    @Volatile
    var gyroReading: FloatArray? = null

    @Volatile
    var gyroSession: Int = 0

    private var nativeGyroSession = 0

    private val appContext = context.applicationContext

    // Outlives nativeViz's handle, which VisualizerView destroys on every detach, and this renderer
    // too (shared by the process, saved across restarts); each new handle starts on the preset it
    // remembers rather than on projectM's idle preset.
    private val milkPreset = MilkPresetMemory.shared(context)
    val nativeViz =
        NativeViz(context).also { viz ->
            viz.onCreated = { milkPreset.restorePath?.let(viz::loadMilkPreset) }
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
            return nativeViz.knows(SceneIds.MILKDROP)
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
     * When no handle exists yet, [NativeViz.create] first queues [MilkPresetMemory.restorePath]
     * through `onCreated`, and the load below then queues [path]. Native keeps one pending request
     * (`Renderer::loadMilkPreset`) and only the GL frame consumes it, so the second replaces the
     * first: the scene never loads the older preset, nor this one twice.
     */
    fun loadMilkPreset(path: String) {
        milkPreset.request(path)
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
        ThermalGovernor.attach(appContext)
        nativeViz.surfaceCreated()
        // The handle may outlive the surface, or be new: either way the next frame resends the whole look.
        lookApplier.reset()
        // A new native renderer starts a new engine: hand it a full window, not the tail of the last.
        // Dropping the feed makes the next frame's cursor a new one.
        nativeViz.feedRing(null)
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

    /** GL thread. Record's tee into the encoder's [surface]; see [GeodeNative.vizRecordStart]. */
    fun recordStart(
        recorder: Long,
        surface: Any,
        width: Int,
        height: Int,
        watermark: Long,
    ): Boolean = nativeViz.recordStart(recorder, surface, width, height, watermark)

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
     * Each frame starts with one native call that takes the pacer's cap and Settings › Performance and
     * says what is due (`core/viz/frame/Cadence`): native skips a repeated quality push, clears a new
     * surface's first frame and builds nothing on it (so `GLSurfaceView.surfaceChanged`, parked on the
     * main thread until this frame ends, is released at once), and picks the frame that reads the
     * thermal state and the one that hands over a new Liquid label. It also takes the version of each
     * pushed input and says which ones this surface still lacks (`core/viz/frame/PushVersions`).
     */
    override fun onDrawFrame(gl: GL10?) {
        val perf = performance
        val due =
            nativeViz.frameBegin(
                pacedRate,
                perf.renderScale,
                perf.marchScale,
                perf.postFx,
                perf.adaptive,
                perf.liquidHot.native,
                paramsVersion,
                lfoVersion,
                adsrVersion,
                tuningVersion,
            )
        if (due and GeodeNative.FRAME_CLEAR_ONLY != 0) return
        if (due and GeodeNative.FRAME_THERMAL_DUE != 0) ThermalGovernor.sample()
        if (due and GeodeNative.FRAME_LIQUID_CHANGED != 0) liquidModeState.value = GeodeNative.frameLiquidMode(due)
        syncNativeState(due)
        nativeViz.render(SystemClock.elapsedRealtimeNanos() / NANOS_PER_SECOND, targetFbo = 0)
        if (due and GeodeNative.FRAME_ERROR_CHANGED != 0) nativeViz.pollError(onShaderError)
        nativeViz.takeMilkPresetLoaded()?.let { path ->
            milkPreset.loaded(path)
            onMilkPresetLoaded(path)
        }
        if (nativeViz.takeDirectorStyleMoments() > 0) onDirectorStyleMoment()
    }

    private fun syncNativeState(due: Int) {
        lookApplier.apply(look, nativeViz, due)
        val reading = gyroReading
        val session = if (reading == null) -1 else gyroSession // off is a session too: one reset, not one a frame
        if (session != nativeGyroSession) nativeViz.gyroReset()
        nativeGyroSession = session
        if (reading != null) nativeViz.gyroSample(reading)
        feedSound()
    }

    // No sample crosses here: the native renderer keeps its own cursor on the ring and takes from it.
    private fun feedSound() = nativeViz.feedRing(soundInput())

    fun exportSceneFactory(sceneId: String): SceneFactory {
        val id = sceneId
        val preset = milkPreset.restorePath
        return object : SceneFactory {
            override val sceneId: String = id

            override val milkPresetPath: String? = preset
        }
    }
}

private const val NANOS_PER_SECOND = 1_000_000_000.0

/** One counter for every pushed input's version (`core/viz/frame/PushVersions`), so each write gets a larger one. */
private val pushVersions = AtomicLong()
