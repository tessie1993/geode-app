package dev.geode.render

import android.content.Context
import android.opengl.GLSurfaceView
import android.os.SystemClock
import dev.geode.analysis.AudioFeatures
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.PcmChunk
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
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

    @Volatile
    var features: AudioFeatures = AudioFeatures.empty()

    @Volatile
    var requestedSceneId: String = SceneIds.DEFAULT

    @Volatile
    var sceneParams: SceneParams = SceneParams.DEFAULT

    /** Vestibular accessibility only; the flash clamp is unconditional inside the native safety pass. */
    @Volatile
    var reducedMotion: Boolean = false

    @Volatile
    var layerSceneId: String? = null

    @Volatile
    var layerMix: Float = 0.5f

    @Volatile
    var layerBlend: BlendMode = BlendMode.SCREEN

    /** The built-in style behind [transitionId], kept for the settings model; the renderer reads the id. */
    @Volatile
    var transitionStyle: TransitionStyle = TransitionStyle.FADE

    @Volatile
    var transitionDurationMs: Long = 1200

    @Volatile
    var transitionId: String = TransitionStyle.FADE.name.lowercase()

    @Volatile
    var onShaderError: (String?) -> Unit = {}

    /** Fired on the GL thread from [onSurfaceChanged], so a UI-layer overlay composer knows the target size. */
    @Volatile
    var onSurfaceSizeChanged: (Int, Int) -> Unit = { _, _ -> }

    // Assigned from Main (EnginePlumbing.kt) and read on the GL thread in onDrawFrame.
    @Volatile
    var onMilkPresetLoaded: (String) -> Unit = {}

    // W02: lets the UI re-decode/re-crop the background image to the surface's new pixel size.
    // Assigned from Main (EnginePlumbing.kt), invoked on the GL thread from onSurfaceChanged.
    @Volatile
    var onSurfaceSizeChanged: (width: Int, height: Int) -> Unit = { _, _ -> }

    @Volatile
    var pcmProvider: () -> PcmChunk? = { null }

    // Config holders only: native ticks its own LfoEngine/AdsrEngine against the configs
    // syncNativeState forwards below (core/viz/RendererFrame.cpp).
    val lfoEngine = LfoEngine()

    val adsrEngine = AdsrEngine()

    private val appContext = context.applicationContext
    private val nativeViz = NativeViz(context)
    private var nativeParams: SceneParams? = null
    private var nativeLfo: List<LfoConfig>? = null
    private var nativeAdsr: List<AdsrConfig>? = null
    private var nativeThermalSampleMs = 0L

    @Volatile
    private var lastMilkPreset: String? = null

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

    fun loadMilkPreset(path: String) {
        lastMilkPreset = path
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

    /** Full-frame ARGB overlay (title/artwork and whatever else layers over the composite). */
    fun setOverlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
    ) = nativeViz.setOverlay(pixels, width, height)

    override fun onSurfaceCreated(
        gl: GL10?,
        config: EGLConfig?,
    ) {
        ThermalGovernor.attach(appContext)
        nativeViz.surfaceCreated()
        nativeParams = null
        nativeLfo = null
        nativeAdsr = null
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

    override fun onDrawFrame(gl: GL10?) {
        nativeViz.setScene(requestedSceneId)
        syncNativeState()
        nativeViz.render(SystemClock.elapsedRealtimeNanos() / NANOS_PER_SECOND, targetFbo = 0)
        nativeViz.pollError(onShaderError)
        nativeViz.takeMilkPresetLoaded()?.let { path ->
            lastMilkPreset = path
            onMilkPresetLoaded(path)
        }
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
        nativeViz.setFeatures(features)
        nativeViz.setReducedMotion(reducedMotion)
        nativeViz.setLayer(layerSceneId, layerMix, layerBlend)
        nativeViz.setTransition(transitionId, transitionDurationMs)
        nativeViz.setPacedFps(ThermalGovernor.pacedFps)
        val now = SystemClock.elapsedRealtime()
        if (now - nativeThermalSampleMs >= THERMAL_SAMPLE_MS) {
            nativeThermalSampleMs = now
            nativeViz.setThermal(ThermalGovernor.platformStatus, ThermalGovernor.thermalHeadroom())
        }
        pcmProvider()?.let { chunk -> nativeViz.pushPcm(chunk.data, chunk.count) }
    }

    fun exportSceneFactory(sceneId: String): SceneFactory {
        val id = sceneId
        val preset = lastMilkPreset
        return object : SceneFactory {
            override val sceneId: String = id

            override val milkPresetPath: String? = preset
        }
    }
}

private const val THERMAL_SAMPLE_MS = 1000L
private const val NANOS_PER_SECOND = 1_000_000_000.0
