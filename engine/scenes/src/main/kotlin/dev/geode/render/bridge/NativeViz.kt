package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
import dev.geode.analysis.AudioFeatures
import dev.geode.engine.bridge.FeatureFrameLayout
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.BlendMode
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.scene.SceneParams
import java.io.File

/**
 * Owns one `geode_viz` handle: the native renderer behind a GL surface or an offscreen target.
 *
 * Durable shader, preset and image configuration is retained across [destroy]/[create]. Other
 * calls are no-ops until [create] (or [surfaceCreated]); the C API's GL-thread rules still apply.
 */
class NativeViz(
    context: Context,
) {
    // Kept referenced for as long as the native AAssetManager resolved from it may be used.
    private val assets: AssetManager = context.assets
    private val cacheDir: String = context.cacheDir.absolutePath
    private val milkTextureDir: String = File(context.filesDir, "milk/textures").absolutePath

    // Every JNI access shares this object's monitor with destroy(). Volatile alone cannot keep
    // a native pointer alive between testing it and entering JNI. GL calls still require GL ownership.
    @Volatile
    private var handle = 0L
    private val paramFrame = FloatArray(SceneParamsCodec.FIELDS)
    private val featureFrame = FloatArray(FeatureFrameLayout.FLOATS)
    private var reportedError = ""
    private val shaders = linkedMapOf<String, String>()
    private var milkPreset: String? = null
    private var injection: Pair<String?, String?>? = null
    private data class Image(val pixels: IntArray?, val width: Int, val height: Int)
    private var overlay: Image? = null
    private var underlay: Image? = null
    private var underlayBlend = 0
    private var underlayAmount = 0f

    val isCreated: Boolean get() = handle != 0L

    @Synchronized
    fun create(): Boolean {
        if (handle == 0L) {
            verifyLayouts()
            handle = GeodeNative.vizCreate(assets, cacheDir)
            if (handle != 0L) {
                GeodeNative.vizSetMilkTextureDir(handle, milkTextureDir)
                shaders.forEach { (id, source) -> GeodeNative.vizSetCustomShader(handle, id, source) }
                milkPreset?.let { GeodeNative.vizLoadMilkPreset(handle, it) }
                injection?.let { GeodeNative.vizSetFluidInjection(handle, it.first, it.second) }
                overlay?.let { GeodeNative.vizSetOverlay(handle, it.pixels, it.width, it.height) }
                underlay?.let { GeodeNative.vizSetUnderlay(handle, it.pixels, it.width, it.height, underlayBlend, underlayAmount) }
                reportedError = ""
            }
        }
        return handle != 0L
    }

    @Synchronized
    fun destroy() {
        if (handle == 0L) return
        // Prefer the last working source over a rejected edit. If a scene has never compiled
        // its queued source (inactive scenes compile lazily), retain that request for replay.
        shaders.keys.toList().forEach { id ->
            GeodeNative.vizCustomShader(handle, id)?.takeIf { it.isNotEmpty() }?.let { shaders[id] = it }
        }
        GeodeNative.vizDestroy(handle)
        handle = 0L
    }

    @Synchronized
    fun knows(sceneId: String): Boolean = handle != 0L && GeodeNative.vizKnows(handle, sceneId)

    @Synchronized
    fun sceneIds(): List<String> = if (handle == 0L) emptyList() else GeodeNative.vizSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    @Synchronized
    fun setParams(p: SceneParams) {
        if (handle == 0L) return
        SceneParamsCodec.pack(p, paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
    }

    @Synchronized
    fun setFeatures(f: AudioFeatures) {
        if (handle == 0L) return
        FeatureFrameCodec.pack(f, featureFrame)
        GeodeNative.vizSetFeatures(handle, featureFrame)
    }

    @Synchronized
    fun setReducedMotion(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetReducedMotion(handle, on)
    }

    @Synchronized
    fun setLayer(
        sceneId: String?,
        mix: Float,
        blend: BlendMode,
    ) {
        if (handle != 0L) GeodeNative.vizSetLayer(handle, sceneId ?: "", mix, blend.ordinal)
    }

    @Synchronized
    fun setTransition(
        id: String,
        durationMs: Long,
    ) {
        if (handle != 0L) GeodeNative.vizSetTransition(handle, id, durationMs)
    }

    @Synchronized
    fun beginParamMorph(seconds: Float) {
        if (handle != 0L) GeodeNative.vizBeginParamMorph(handle, seconds)
    }

    @Synchronized
    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSetTouch(handle, xy, n)
    }

    @Synchronized
    fun queueTouchStroke(
        nx: Float,
        ny: Float,
        ndx: Float,
        ndy: Float,
        dt: Float,
        strength: Float,
    ) {
        if (handle != 0L) GeodeNative.vizQueueTouchStroke(handle, nx, ny, ndx, ndy, dt, strength)
    }

    @Synchronized
    fun setFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) {
        injection = force to dye
        if (handle != 0L) GeodeNative.vizSetFluidInjection(handle, force, dye)
    }

    @Synchronized
    fun loadMilkPreset(path: String) {
        milkPreset = path
        if (handle != 0L) GeodeNative.vizLoadMilkPreset(handle, path)
    }

    @Synchronized
    fun reloadMilkPreset() {
        if (handle != 0L) GeodeNative.vizReloadMilkPreset(handle)
    }

    /** The preset MilkDrop last compiled, handed out once; null until the next one. */
    @Synchronized
    fun takeMilkPresetLoaded(): String? =
        if (handle == 0L) null else GeodeNative.vizTakeMilkPresetLoaded(handle)?.also { milkPreset = it }

    @Synchronized
    fun pushPcm(
        mono: FloatArray,
        count: Int,
    ) {
        if (handle != 0L && count > 0) GeodeNative.vizPushPcm(handle, mono, count)
    }

    @Synchronized
    fun setCustomShader(
        sceneId: String,
        fragmentSource: String,
    ) {
        shaders[sceneId] = fragmentSource
        if (handle != 0L) GeodeNative.vizSetCustomShader(handle, sceneId, fragmentSource)
    }

    @Synchronized
    fun customShaderFor(sceneId: String): String? =
        if (handle != 0L) GeodeNative.vizCustomShader(handle, sceneId) else shaders[sceneId]?.takeIf { it.isNotEmpty() }

    @Synchronized
    fun setLfoConfigs(configs: List<LfoConfig>) {
        if (handle == 0L) return
        configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(handle, slot, ModConfigCodec.packLfo(c)) }
    }

    @Synchronized
    fun setAdsrConfigs(configs: List<AdsrConfig>) {
        if (handle == 0L) return
        configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(handle, slot, ModConfigCodec.packAdsr(c)) }
    }

    @Synchronized
    fun setThermal(
        platformStatus: Int,
        headroom: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetThermal(handle, platformStatus, headroom)
    }

    @Synchronized
    fun setPacedFps(fps: Float) {
        if (handle != 0L) GeodeNative.vizSetPacedFps(handle, fps)
    }

    @Synchronized
    fun setOffscreen(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetOffscreen(handle, on)
    }

    @Synchronized
    fun surfaceCreated() {
        if (create()) GeodeNative.vizSurfaceCreated(handle)
    }

    @Synchronized
    fun surfaceChanged(
        width: Int,
        height: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSurfaceChanged(handle, width, height)
    }

    @Synchronized
    fun setScene(sceneId: String): Boolean = handle != 0L && GeodeNative.vizSetScene(handle, sceneId)

    @Synchronized
    fun warmTransition(id: String) {
        if (handle != 0L) GeodeNative.vizWarmTransition(handle, id)
    }

    @Synchronized
    fun cut() {
        if (handle != 0L) GeodeNative.vizCut(handle)
    }

    @Synchronized
    fun render(
        timeSeconds: Double,
        targetFbo: Int,
    ) {
        if (handle != 0L) GeodeNative.vizRender(handle, timeSeconds, targetFbo)
    }

    @Synchronized
    fun releaseScenes() {
        if (handle != 0L) GeodeNative.vizReleaseScenes(handle)
    }

    /** Full-frame RGBA8 overlay drawn last, premultiplied alpha; null clears. Any thread; latched for the next frame. */
    @Synchronized
    fun setOverlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
    ) {
        overlay = Image(pixels?.copyOf(), width, height)
        if (handle != 0L) GeodeNative.vizSetOverlay(handle, pixels, width, height)
    }

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: blend 0 = replace scene where scene is
     * black (screen), 1 = multiply, 2 = add; amount 0..1. null clears. Any thread; latched.
     */
    @Synchronized
    fun setUnderlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    ) {
        underlay = Image(pixels?.copyOf(), width, height)
        underlayBlend = blend
        underlayAmount = amount
        if (handle != 0L) GeodeNative.vizSetUnderlay(handle, pixels, width, height, blend, amount)
    }

    /** Reports the native error state once per change: a message, or null when it clears. */
    @Synchronized
    fun pollError(onChange: (String?) -> Unit) {
        if (handle == 0L) return
        val error = GeodeNative.vizLastError(handle)
        if (error == reportedError) return
        reportedError = error
        onChange(error.ifEmpty { null })
    }

    private companion object {
        @Volatile
        private var verified = false

        fun verifyLayouts() {
            if (verified) return
            SceneParamsCodec.verify(GeodeNative.vizParamNames())
            check(GeodeNative.featureFrameFloats() == FeatureFrameLayout.FLOATS) {
                "GeodeFeatureFrame has ${GeodeNative.featureFrameFloats()} floats, FeatureFrameLayout expects ${FeatureFrameLayout.FLOATS}"
            }
            verified = true
        }
    }
}
