package dev.geode.render

import android.content.Context
import android.opengl.GLSurfaceView
import android.os.SystemClock
import dev.geode.engine.audio.SoundCursor
import dev.geode.engine.audio.SoundInput
import dev.geode.engine.audio.SoundTuning
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
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
    companion object {
        /** The floats a parameter fade must never glide, and why. */
        val NOT_FADED: Map<String, String> =
            mapOf(
                "paramFadeSec" to "the fade's own time constant: gliding it would make every fade chase a moving target",
                "paletteBaseOverride" to "UNSET_OVERRIDE (-1) is a sentinel: a glide through it flickers between set and unset",
                "paletteRangeOverride" to "UNSET_OVERRIDE sentinel, as paletteBaseOverride",
                "palette2BaseOverride" to "UNSET_OVERRIDE sentinel, as paletteBaseOverride",
                "palette2RangeOverride" to "UNSET_OVERRIDE sentinel, as paletteBaseOverride",
            )
    }

    /**
     * Until a look is pushed, the app's first style (FreeTier.FIRST_STYLE_ID in :app): the wallpaper
     * and dream with no saved look then show the free showpiece rather than silk, a Pro renderer.
     */
    @Volatile
    var requestedSceneId: String = SceneIds.NEON_CHROME_SEA

    @Volatile
    var sceneParams: SceneParams = SceneParams.DEFAULT
        set(value) {
            field = value
            paramsVersion = pushVersions.incrementAndGet()
        }

    @Volatile private var paramsVersion = 0L

    /** Vestibular accessibility only; the flash clamp is unconditional inside the native safety pass. */
    @Volatile
    var reducedMotion: Boolean = false

    /**
     * Settings › Performance. Live surfaces only: the offscreen export renderer never reads it,
     * because an export is not real time and can afford full quality. The fps cap is not here;
     * it belongs to the view's [FramePacer] (see [VisualizerView.applyPerformance]).
     */
    @Volatile
    var performance: PerformanceSettings = PerformanceSettings.HIGH

    /** The in-app stage, the one surface a capture take copies (ADR 0008); wallpaper, dream and cast leave it off. */
    @Volatile
    var takeStage: Boolean = false

    @Volatile
    var transitionDurationMs: Long = 1200

    @Volatile
    var transitionId: String = TransitionStyle.FADE.name.lowercase()

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
     * The audio this surface's sound engine hears (core/sound, owned by the native renderer). Read on the
     * GL thread each frame through [soundCursor], so the engine gets every sample once, in order, and is
     * stepped with this surface's own frame time. Asked every frame, so a playback session that comes and
     * goes under a running wallpaper is picked up. Null: the engine hears nothing and its frame stays silent.
     */
    @Volatile
    var soundInput: () -> SoundInput? = { null }

    private var soundCursor: SoundCursor? = null

    /** Settings › Audio. Every assignment is a new version, pushed on the frame native asks for it. */
    @Volatile
    var soundTuning: SoundTuning = SoundTuning()
        set(value) {
            field = value
            tuningVersion = pushVersions.incrementAndGet()
        }

    @Volatile private var tuningVersion = 0L

    val lfoEngine = LfoEngine()

    val adsrEngine = AdsrEngine()

    /**
     * The user's own reaction routes, or null to play the style's DNA
     * ([BuildStyleReactionsUseCase] for the current scene and `reactSeed`). A list plays only on the
     * moments [HandMask] gives the user; every other moment plays the live DNA ([StyleReactionsCache]).
     */
    @Volatile
    var reactions: List<ReactionRoute>? = null

    /** Gyro look, -1..1 per axis, written by the sensor listener; native reads it only while camGyro is on. */
    @Volatile
    var lookYaw: Float = 0f

    @Volatile
    var lookPitch: Float = 0f

    private val styleReactions = StyleReactionsCache()

    private val appContext = context.applicationContext

    // Outlives nativeViz's handle, which VisualizerView destroys on every detach, and this renderer
    // too (shared by the process, saved across restarts); each new handle starts on the preset it
    // remembers rather than on projectM's idle preset.
    private val milkPreset = MilkPresetMemory.shared(context)
    private val nativeViz =
        NativeViz(context).also { viz ->
            viz.onCreated = { milkPreset.restorePath?.let(viz::loadMilkPreset) }
        }
    private var nativeReactions: List<ReactionRoute>? = null
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

    fun queueTouchStroke(
        nx: Float,
        ny: Float,
        ndx: Float,
        ndy: Float,
        dt: Float,
        strength: Float,
    ) = nativeViz.queueTouchStroke(nx, ny, ndx, ndy, dt, strength)

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
        nativeReactions = null
        // A new native renderer starts a new engine: hand it a full window, not the tail of the last.
        soundCursor = null
    }

    override fun onSurfaceChanged(
        gl: GL10?,
        width: Int,
        height: Int,
    ) {
        nativeViz.surfaceChanged(width, height)
        onSurfaceSizeChanged(width, height)
    }

    /**
     * Full-frame RGBA8 background image blended under the scene; see [dev.geode.render.bridge.NativeViz.setUnderlay].
     * Any thread; latched for the next frame, so the UI calls this directly (or via
     * `VisualizerView.queueEvent {}`) rather than waiting for [onDrawFrame].
     */
    fun setUnderlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: UnderlayBlend,
        amount: Float,
    ) = nativeViz.setUnderlay(pixels, width, height, blend.ordinal, amount)

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
                ThermalGovernor.pacedFps,
                perf.renderScale,
                perf.marchScale,
                perf.postFx,
                perf.adaptive,
                perf.liquidHot.native,
                paramsVersion,
                lfoEngine.version,
                adsrEngine.version,
                tuningVersion,
            )
        if (due and GeodeNative.FRAME_CLEAR_ONLY != 0) return
        if (due and GeodeNative.FRAME_THERMAL_DUE != 0) ThermalGovernor.sample()
        if (due and GeodeNative.FRAME_LIQUID_CHANGED != 0) liquidModeState.value = GeodeNative.frameLiquidMode(due)
        nativeViz.setScene(requestedSceneId)
        syncNativeState(due)
        nativeViz.render(SystemClock.elapsedRealtimeNanos() / NANOS_PER_SECOND, targetFbo = 0)
        nativeViz.pollError(onShaderError)
        nativeViz.takeMilkPresetLoaded()?.let { path ->
            milkPreset.loaded(path)
            onMilkPresetLoaded(path)
        }
        if (nativeViz.takeDirectorStyleMoments() > 0) onDirectorStyleMoment()
    }

    private fun syncNativeState(due: Int) {
        val p = sceneParams
        if (due and GeodeNative.FRAME_PUSH_PARAMS != 0) nativeViz.setParams(p)
        if (due and GeodeNative.FRAME_PUSH_LFO != 0) nativeViz.setLfoConfigs(lfoEngine.configs)
        if (due and GeodeNative.FRAME_PUSH_ADSR != 0) nativeViz.setAdsrConfigs(adsrEngine.configs)
        if (due and GeodeNative.FRAME_PUSH_TUNING != 0) nativeViz.setSoundTuning(soundTuning.toFloats())
        val routes = styleReactions.routesFor(requestedSceneId, p.reactSeed, reactions, HandMask.current)
        if (routes !== nativeReactions) {
            nativeViz.setReactions(routes)
            nativeReactions = routes
        }
        nativeViz.setLook(lookYaw, lookPitch)
        nativeViz.setReducedMotion(reducedMotion)
        nativeViz.setTakeStage(takeStage)
        nativeViz.setTransition(transitionId, transitionDurationMs)
        feedSound()
    }

    private fun feedSound() {
        val input = soundInput() ?: return
        val cursor = soundCursor?.takeIf { it.ring === input.ring } ?: SoundCursor(input.ring).also { soundCursor = it }
        val take = cursor.take()
        if (take.restarted) nativeViz.resetAudio()
        nativeViz.writeAudio(cursor.interleaved, take.frames, cursor.channels, input.sampleRateHz)
    }

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

/** Modulation configs the UI edits; native ticks them (core/viz/RendererFrame.cpp) and asks for a push by [version]. */
open class PushedConfigs<T>(
    initial: List<T>,
) {
    @Volatile
    var configs: List<T> = initial
        set(value) {
            field = value
            version = pushVersions.incrementAndGet()
        }

    @Volatile var version = 0L
        private set
}
