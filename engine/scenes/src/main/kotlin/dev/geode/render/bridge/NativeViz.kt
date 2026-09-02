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

/**
 * Owns one `geode_viz` handle: the native renderer behind a GL surface or an offscreen target.
 *
 * Every call is a no-op until [create] (or [surfaceCreated]) has run; the GL-thread rules of the
 * C API apply unchanged.
 */
class NativeViz(
    context: Context,
) {
    // Kept referenced for as long as the native AAssetManager resolved from it may be used.
    private val assets: AssetManager = context.assets
    private val cacheDir: String = context.cacheDir.absolutePath
    private var handle = 0L
    private val paramFrame = FloatArray(SceneParamsCodec.FIELDS)
    private val featureFrame = FloatArray(FeatureFrameLayout.FLOATS)
    private var reportedError = ""

    val isCreated: Boolean get() = handle != 0L

    fun create(): Boolean {
        if (handle == 0L) {
            verifyLayouts()
            handle = GeodeNative.vizCreate(assets, cacheDir)
        }
        return handle != 0L
    }

    fun destroy() {
        if (handle == 0L) return
        GeodeNative.vizDestroy(handle)
        handle = 0L
    }

    fun knows(sceneId: String): Boolean = handle != 0L && GeodeNative.vizKnows(handle, sceneId)

    fun sceneIds(): List<String> =
        if (handle == 0L) emptyList() else GeodeNative.vizSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    fun setParams(p: SceneParams) {
        if (handle == 0L) return
        SceneParamsCodec.pack(p, paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
    }

    fun setFeatures(f: AudioFeatures) {
        if (handle == 0L) return
        FeatureFrameCodec.pack(f, featureFrame)
        GeodeNative.vizSetFeatures(handle, featureFrame)
    }

    fun setReducedMotion(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetReducedMotion(handle, on)
    }

    fun setLayer(
        sceneId: String?,
        mix: Float,
        blend: BlendMode,
    ) {
        if (handle != 0L) GeodeNative.vizSetLayer(handle, sceneId ?: "", mix, blend.ordinal)
    }

    fun setTransition(
        id: String,
        durationMs: Long,
    ) {
        if (handle != 0L) GeodeNative.vizSetTransition(handle, id, durationMs)
    }

    fun beginParamMorph(seconds: Float) {
        if (handle != 0L) GeodeNative.vizBeginParamMorph(handle, seconds)
    }

    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSetTouch(handle, xy, n)
    }

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

    fun setFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) {
        if (handle != 0L) GeodeNative.vizSetFluidInjection(handle, force, dye)
    }

    fun pushPcm(
        mono: FloatArray,
        count: Int,
    ) {
        if (handle != 0L && count > 0) GeodeNative.vizPushPcm(handle, mono, count)
    }

    fun setCustomShader(
        sceneId: String,
        fragmentSource: String,
    ) {
        if (handle != 0L) GeodeNative.vizSetCustomShader(handle, sceneId, fragmentSource)
    }

    fun customShaderFor(sceneId: String): String? = if (handle == 0L) null else GeodeNative.vizCustomShader(handle, sceneId)

    fun setLfoConfigs(configs: List<LfoConfig>) {
        if (handle == 0L) return
        configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(handle, slot, ModConfigCodec.packLfo(c)) }
    }

    fun setAdsrConfigs(configs: List<AdsrConfig>) {
        if (handle == 0L) return
        configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(handle, slot, ModConfigCodec.packAdsr(c)) }
    }

    fun setThermal(
        platformStatus: Int,
        headroom: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetThermal(handle, platformStatus, headroom)
    }

    fun setPacedFps(fps: Float) {
        if (handle != 0L) GeodeNative.vizSetPacedFps(handle, fps)
    }

    fun setOffscreen(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetOffscreen(handle, on)
    }

    fun surfaceCreated() {
        if (create()) GeodeNative.vizSurfaceCreated(handle)
    }

    fun surfaceChanged(
        width: Int,
        height: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSurfaceChanged(handle, width, height)
    }

    fun setScene(sceneId: String): Boolean = handle != 0L && GeodeNative.vizSetScene(handle, sceneId)

    fun warmTransition(id: String) {
        if (handle != 0L) GeodeNative.vizWarmTransition(handle, id)
    }

    fun cut() {
        if (handle != 0L) GeodeNative.vizCut(handle)
    }

    fun render(
        timeSeconds: Double,
        targetFbo: Int,
    ) {
        if (handle != 0L) GeodeNative.vizRender(handle, timeSeconds, targetFbo)
    }

    fun releaseScenes() {
        if (handle != 0L) GeodeNative.vizReleaseScenes(handle)
    }

    /** Reports the native error state once per change: a message, or null when it clears. */
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
