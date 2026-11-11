package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.audio.SoundTuning
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ReactionRoute
import dev.geode.render.look.LookTarget
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.SceneParams
import java.io.File
import java.lang.ref.Reference

/**
 * Owns one `geode_viz` handle: the native renderer behind a GL surface or an offscreen target.
 *
 * Every call is a no-op until [create] (or [surfaceCreated]) has run; the GL-thread rules of the
 * C API apply unchanged.
 */
class NativeViz(
    context: Context,
) : LookTarget {
    // Kept referenced for as long as the native AAssetManager resolved from it may be used.
    private val assets: AssetManager = context.assets
    private val cacheDir: String = context.cacheDir.absolutePath
    private val milkTextureDir: String = File(context.filesDir, "milk/textures").absolutePath

    // Written on the GL thread (surfaceCreated) and read/written from composition-time call
    // sites on the main thread (e.g. milkdropAvailable, loadMilkPreset); @Volatile keeps every
    // thread's read of a just-published handle current, and create()/destroy() are synchronised
    // so two threads racing on a 0L handle cannot both create (and leak) a renderer.
    @Volatile
    private var handle = 0L
    private val paramFrame = FloatArray(SceneParamsCodec.FIELDS)
    private val sent = LastSent()
    private var reportedError = ""

    /**
     * Runs once per new handle, inside [create], on whichever thread created it. A handle is
     * created again after every [destroy], and that new renderer knows none of the state the old
     * one was given; this is where an owner re-applies what must survive (the MilkDrop preset).
     */
    var onCreated: () -> Unit = {}

    @Synchronized
    fun create(): Boolean {
        if (handle == 0L) {
            handle = GeodeNative.vizCreate(assets, cacheDir)
            sent.clear()
            if (handle != 0L) {
                GeodeNative.vizSetMilkTextureDir(handle, milkTextureDir)
                applyUiBlur()
                edgeGlow?.let { GeodeNative.vizSetEdgeGlow(handle, it) }
                onCreated()
            }
        }
        return handle != 0L
    }

    @Synchronized
    fun destroy() {
        if (handle == 0L) return
        GeodeNative.vizDestroy(handle)
        handle = 0L
    }

    fun knows(sceneId: String): Boolean = handle != 0L && GeodeNative.vizKnows(handle, sceneId)

    fun sceneIds(): List<String> = if (handle == 0L) emptyList() else GeodeNative.vizSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    override fun setParams(p: SceneParams) {
        if (handle == 0L) return
        SceneParamsCodec.pack(p, paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
    }

    override fun setReducedMotion(on: Boolean) {
        if (handle != 0L && sent.reducedMotion(on)) GeodeNative.vizSetReducedMotion(handle, on)
    }

    override fun setTransition(
        id: String,
        durationMs: Long,
    ) {
        if (handle != 0L && sent.transition(id, durationMs)) GeodeNative.vizSetTransition(handle, id, durationMs)
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

    fun loadMilkPreset(path: String) {
        if (handle != 0L) GeodeNative.vizLoadMilkPreset(handle, path)
    }

    fun reloadMilkPreset() {
        if (handle != 0L) GeodeNative.vizReloadMilkPreset(handle)
    }

    /** The preset MilkDrop last compiled, handed out once; null until the next one. */
    fun takeMilkPresetLoaded(): String? = if (handle == 0L) null else GeodeNative.vizTakeMilkPresetLoaded(handle)

    /** The director's new-section and drop decisions since the last call (one Auto mode). */
    fun takeDirectorStyleMoments(): Int = if (handle == 0L) 0 else GeodeNative.vizTakeDirectorStyleMoments(handle)

    /** The renderer's sound engine: interleaved PCM in play order, each sample once, before [render]. */
    fun writeAudio(
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRateHz: Int,
    ) {
        if (handle != 0L && frames > 0) GeodeNative.vizWriteAudio(handle, interleaved, frames, channels, sampleRateHz)
    }

    /**
     * The live feed: the renderer's sound engine takes what [ring] gained since the last frame, natively,
     * each sample once, and starts over on a new epoch or another ring. Null: it hears nothing and lets
     * the last ring go.
     */
    fun feedRing(ring: SampleRing?) {
        if (handle != 0L) GeodeNative.vizFeedRing(handle, ring?.handle ?: 0L)
        // The ring's handle is only valid while the ring is held: hold it past the call.
        Reference.reachabilityFence(ring)
    }

    /** Settings › Audio. The floats are read here, not by the caller: the defaults are native. */
    override fun setSoundTuning(tuning: SoundTuning) {
        if (handle != 0L) GeodeNative.vizSetSoundTuning(handle, tuning.toFloats())
    }

    override fun setLfoConfigs(configs: List<LfoConfig>) {
        if (handle == 0L) return
        configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(handle, slot, ModConfigCodec.packLfo(c)) }
    }

    override fun setAdsrConfigs(configs: List<AdsrConfig>) {
        if (handle == 0L) return
        configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(handle, slot, ModConfigCodec.packAdsr(c)) }
    }

    /** The user's routes and hand mask, sent only when [routes] (by identity) or [mask] changes; native resolves what plays. */
    override fun setUserReactions(
        routes: List<ReactionRoute>?,
        mask: Int,
    ) {
        if (handle != 0L && sent.userReactions(routes, mask)) {
            GeodeNative.vizSetUserReactions(handle, routes?.let { BuildStyleReactionsUseCase.packExact(it) }, mask)
        }
    }

    fun gyroSample(
        azimuth: Float,
        pitch: Float,
    ) {
        if (handle != 0L) GeodeNative.vizGyroSample(handle, azimuth, pitch)
    }

    fun gyroReset() {
        if (handle != 0L) GeodeNative.vizGyroReset(handle)
    }

    /** One live frame's start; see [GeodeNative.vizFrameBegin]. 0 before the handle exists. */
    fun frameBegin(
        pacedFps: Float,
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
        liquidHotPolicy: Int,
        paramsVersion: Long,
        lfoVersion: Long,
        adsrVersion: Long,
        tuningVersion: Long,
    ): Int {
        if (handle == 0L) return 0
        return GeodeNative.vizFrameBegin(
            handle,
            pacedFps,
            renderScale,
            marchScale,
            postFx,
            adaptive,
            liquidHotPolicy,
            paramsVersion,
            lfoVersion,
            adsrVersion,
            tuningVersion,
        )
    }

    /** Pins an export's engine settings and returns the pinned hand mask; see [GeodeNative.vizPinEngineSettings]. */
    fun pinEngineSettings(
        entitled: Boolean,
        durationMs: Long,
    ): Int = if (handle != 0L) GeodeNative.vizPinEngineSettings(handle, entitled, durationMs) else 0

    fun setOffscreen(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetOffscreen(handle, on)
    }

    /**
     * The glass sheet's blur region (left, top, right, bottom, strength, corner px) and the wallpaper's
     * edge glow options, kept here so a new handle (one per attach, see [create]) starts with what its
     * owner last set; the native renderer keeps both across a new surface itself.
     */
    private val uiBlur = FloatArray(UI_BLUR_FLOATS)
    private var edgeGlow: FloatArray? = null

    /** Pins this renderer for an export; see [GeodeNative.vizPrepareOffscreen]. */
    fun prepareOffscreen() {
        if (handle != 0L) GeodeNative.vizPrepareOffscreen(handle)
    }

    /** See [GeodeNative.vizSetFrameRate]. A no-op before the handle exists. */
    fun setFrameRate(fps: Int) {
        if (handle != 0L) GeodeNative.vizSetFrameRate(handle, fps)
    }

    /** The glass sheet's blur region; see [GeodeNative.vizSetUiBlurRegion]. Latched before the handle exists. */
    @Synchronized
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

    /** The wallpaper's edge glow; see [GeodeNative.vizSetEdgeGlow]. Any thread; latched before the handle exists. */
    @Synchronized
    fun setEdgeGlow(options: FloatArray) {
        edgeGlow = options.copyOf()
        if (handle != 0L) GeodeNative.vizSetEdgeGlow(handle, edgeGlow)
    }

    private fun applyUiBlur() {
        if (handle != 0L) GeodeNative.vizSetUiBlurRegion(handle, uiBlur[0], uiBlur[1], uiBlur[2], uiBlur[3], uiBlur[4], uiBlur[5])
    }

    /** GL thread. Record's tee into the encoder's [surface]; see [GeodeNative.vizRecordStart]. */
    fun recordStart(
        recorder: Long,
        surface: Any,
        width: Int,
        height: Int,
        watermark: Long,
    ): Boolean = handle != 0L && GeodeNative.vizRecordStart(handle, recorder, surface, width, height, watermark)

    fun recordStop() {
        if (handle != 0L) GeodeNative.vizRecordStop(handle)
    }

    fun surfaceCreated() {
        // A new surface may be a new GL context: send every value once more, as the renderer does.
        sent.clear()
        if (create()) GeodeNative.vizSurfaceCreated(handle)
    }

    fun surfaceChanged(
        width: Int,
        height: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSurfaceChanged(handle, width, height)
    }

    override fun setScene(sceneId: String): Boolean = handle != 0L && GeodeNative.vizSetScene(handle, sceneId)

    /** An export segment's style switches; see [GeodeNative.vizSetStyleSchedule]. */
    fun setStyleSchedule(schedule: StyleSchedule) {
        if (handle == 0L || schedule.isEmpty) return
        GeodeNative.vizSetStyleSchedule(
            handle,
            schedule.cues.map { it.atMs.toDouble() }.toDoubleArray(),
            schedule.cues.map { it.sceneId }.toTypedArray(),
            schedule.cues.map { it.milkPath.orEmpty() }.toTypedArray(),
            schedule.beatCutIds.toTypedArray(),
            schedule.everyBeats,
            schedule.lockedStyleIds.toTypedArray(),
        )
    }

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

    /** The surface is about to stop drawing: the spare scenes go now. GL thread, context still current. */
    fun trimSpares() {
        if (handle != 0L) GeodeNative.vizTrimSpares(handle)
    }

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: blend 0 = replace scene where scene is
     * black (screen), 1 = multiply, 2 = add; amount 0..1. null clears. Any thread; latched.
     */
    override fun setUnderlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetUnderlay(handle, pixels, width, height, blend, amount)
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
        const val UI_BLUR_FLOATS = 6
    }
}

/**
 * What a handle was last sent of the values the live renderer hands over every frame (the look,
 * reduced motion, the transition, the paced rate), so each crosses JNI only when it changes, as
 * the renderer's params, LFOs and quality already do. Each check records the value it lets
 * through; [clear] makes the next of each go through again (a new handle or surface). GL thread.
 */
internal class LastSent {
    private var yaw = Float.NaN
    private var pitch = Float.NaN
    private var reduced: Boolean? = null
    private var transitionId: String? = null
    private var transitionMs = -1L
    private var fps = Float.NaN
    private var routes: List<ReactionRoute>? = null
    private var routesMask = 0
    private var routesSent = false

    fun look(
        y: Float,
        p: Float,
    ): Boolean {
        if (y == yaw && p == pitch) return false
        yaw = y
        pitch = p
        return true
    }

    fun reducedMotion(on: Boolean): Boolean = (on != reduced).also { if (it) reduced = on }

    fun transition(
        id: String,
        ms: Long,
    ): Boolean {
        if (id == transitionId && ms == transitionMs) return false
        transitionId = id
        transitionMs = ms
        return true
    }

    fun pacedFps(f: Float): Boolean = (f != fps).also { if (it) fps = f }

    fun userReactions(
        r: List<ReactionRoute>?,
        mask: Int,
    ): Boolean {
        if (routesSent && r === routes && mask == routesMask) return false
        routes = r
        routesMask = mask
        routesSent = true
        return true
    }

    fun clear() {
        yaw = Float.NaN
        pitch = Float.NaN
        reduced = null
        transitionId = null
        transitionMs = -1L
        fps = Float.NaN
        routes = null
        routesMask = 0
        routesSent = false
    }
}
