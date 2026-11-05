package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.SystemClock
import dev.geode.engine.audio.SoundCursor
import dev.geode.engine.audio.SoundInput
import dev.geode.engine.audio.SoundTuning
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.PcmChunk
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
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

    @Volatile
    var layerSceneId: String? = null

    @Volatile
    var layerMix: Float = 0.5f

    @Volatile
    var layerBlend: BlendMode = BlendMode.SCREEN

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

    @Volatile
    var pcmProvider: () -> PcmChunk? = { null }

    /**
     * The audio this surface's sound engine hears (core/sound, owned by the native renderer). Read on the
     * GL thread each frame through [soundCursor], so the engine gets every sample once, in order, and is
     * stepped with this surface's own frame time. Asked every frame, so a playback session that comes and
     * goes under a running wallpaper is picked up. Null: the engine hears nothing and its frame stays silent.
     */
    @Volatile
    var soundInput: () -> SoundInput? = { null }

    private var soundCursor: SoundCursor? = null

    /** Settings › Audio. Compared by identity on the GL thread, so assign a new instance to change it. */
    @Volatile
    var soundTuning: SoundTuning = SoundTuning()

    private var nativeSoundTuning: SoundTuning? = null

    // Config holders only: native ticks its own LfoEngine/AdsrEngine against the configs
    // syncNativeState forwards below (core/viz/RendererFrame.cpp).
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
    private var nativeParams: SceneParams? = null
    private var nativeLfo: List<LfoConfig>? = null
    private var nativeAdsr: List<AdsrConfig>? = null
    private var nativeReactions: List<ReactionRoute>? = null
    private var nativePerformance: PerformanceSettings? = null
    private val liquidModeState = MutableStateFlow(0)

    /**
     * What Liquid mode ran last frame (`liquid::Mode`: 0 off, 1 full, 2 lite, 3 warp, 4 paused), so
     * Customize can say when a hot phone simplified or paused it. The native renderer owns the
     * answer on the GL thread and hands it over only when it changes, so a collector is never woken
     * for a label that stayed the same.
     */
    val liquidMode: StateFlow<Int> = liquidModeState.asStateFlow()

    /**
     * True from [onSurfaceCreated] until the first frame on that surface has been presented.
     *
     * `GLSurfaceView.surfaceChanged` runs on the main thread and parks it in
     * `GLThread.onWindowResize` until the GL thread has finished the next frame. A new surface's
     * first real frame is the expensive one: it builds the scene and compiles every shader it
     * needs, which on a software GPU (the CI emulator's swiftshader) takes ~5 s. That held the
     * main thread long enough for input dispatch to time out, an ANR that kills the process.
     * So the first frame is only a clear: the main thread is released at once, and the build runs
     * on the next frame the pacer asks for, where nobody waits on it. The screen is black for that
     * one frame, as a new surface already is.
     */
    private var firstFrameOnSurface = false

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

    fun submitShader(
        sceneId: String,
        fragmentSrc: String,
    ) {
        nativeViz.create()
        nativeViz.setCustomShader(sceneId, fragmentSrc)
    }

    fun customShaderFor(sceneId: String): String? = nativeViz.customShaderFor(sceneId)

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
        applyUiBlur()
        nativeParams = null
        nativeLfo = null
        nativeAdsr = null
        nativeReactions = null
        nativePerformance = null
        nativeSoundTuning = null
        // A new native renderer starts a new engine: hand it a full window, not the tail of the last.
        soundCursor = null
        firstFrameOnSurface = true
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

    /**
     * The glass sheet's blur region (left, top, right, bottom, strength, corner px), kept so a new
     * native handle (one per attach) and a new surface start with the sheet the UI last set. GL thread
     * only: [VisualizerView.setUiBlurRegion] posts here.
     */
    private val uiBlur = FloatArray(UI_BLUR_FLOATS)

    /** GL thread; see [VisualizerView.setUiBlurRegion] for the contract. */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) {
        uiBlur[0] = left
        uiBlur[1] = top
        uiBlur[2] = right
        uiBlur[3] = bottom
        uiBlur[4] = strength
        uiBlur[5] = cornerPx
        applyUiBlur()
    }

    private fun applyUiBlur() = nativeViz.setUiBlurRegion(uiBlur[0], uiBlur[1], uiBlur[2], uiBlur[3], uiBlur[4], uiBlur[5])

    override fun onDrawFrame(gl: GL10?) {
        if (firstFrameOnSurface) {
            firstFrameOnSurface = false
            GLES30.glClearColor(0f, 0f, 0f, 1f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
            return
        }
        nativeViz.setScene(requestedSceneId)
        syncNativeState()
        nativeViz.render(SystemClock.elapsedRealtimeNanos() / NANOS_PER_SECOND, targetFbo = 0)
        nativeViz.pollError(onShaderError)
        nativeViz.takeMilkPresetLoaded()?.let { path ->
            milkPreset.loaded(path)
            onMilkPresetLoaded(path)
        }
        if (nativeViz.takeDirectorStyleMoments() > 0) onDirectorStyleMoment()
    }

    private fun syncNativeState() {
        val p = sceneParams
        if (p !== nativeParams) {
            nativeViz.setParams(p)
            nativeParams = p
        }
        val lfo = lfoEngine.configs
        if (lfo !== nativeLfo) {
            nativeViz.setLfoConfigs(lfo)
            nativeLfo = lfo
        }
        val adsr = adsrEngine.configs
        if (adsr !== nativeAdsr) {
            nativeViz.setAdsrConfigs(adsr)
            nativeAdsr = adsr
        }
        val routes = styleReactions.routesFor(requestedSceneId, p.reactSeed, reactions, HandMask.current)
        if (routes !== nativeReactions) {
            nativeViz.setReactions(routes)
            nativeReactions = routes
        }
        val perf = performance
        if (perf != nativePerformance) {
            nativeViz.setQuality(perf.renderScale, perf.marchScale, perf.postFx, perf.adaptive)
            nativeViz.setLiquidHotPolicy(perf.liquidHot.native)
            nativePerformance = perf
        }
        nativeViz.setLook(lookYaw, lookPitch)
        nativeViz.setReducedMotion(reducedMotion)
        nativeViz.setLayer(layerSceneId, layerMix, layerBlend)
        nativeViz.setTransition(transitionId, transitionDurationMs)
        nativeViz.setPacedFps(ThermalGovernor.pacedFps)
        // Both cadences are native: the thermal reading once a second for the process, the liquid label on a change.
        ThermalGovernor.sample()
        nativeViz.takeLiquidMode().let { if (it >= 0) liquidModeState.value = it }
        pcmProvider()?.let { chunk -> nativeViz.pushPcm(chunk.data, chunk.count) }
        val tuning = soundTuning
        if (tuning !== nativeSoundTuning) {
            nativeViz.setSoundTuning(tuning.toFloats())
            nativeSoundTuning = tuning
        }
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

private const val UI_BLUR_FLOATS = 6
private const val NANOS_PER_SECOND = 1_000_000_000.0
