package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
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
import java.util.concurrent.locks.ReentrantReadWriteLock

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

    // Created on the GL thread (surfaceCreated) or the main thread (milkdropAvailable,
    // loadMilkPreset), destroyed on the GL thread. Every native call goes through [guard], so a
    // destroy cannot free the renderer under a call in flight on another thread (#853).
    private val guard = HandleGuard()
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
    fun create(): Boolean =
        guard.create({ GeodeNative.vizCreate(assets, cacheDir) }) { h ->
            sent.clear()
            GeodeNative.vizSetMilkTextureDir(h, milkTextureDir)
            applyUiBlur()
            edgeGlow?.let { GeodeNative.vizSetEdgeGlow(h, it) }
            onCreated()
        }

    @Synchronized
    fun destroy() {
        guard.destroy(GeodeNative::vizDestroy)
    }

    fun knows(sceneId: String): Boolean = guard.use(false) { h -> GeodeNative.vizKnows(h, sceneId) }

    fun sceneIds(): List<String> = guard.use(emptyList()) { h -> GeodeNative.vizSceneIds(h).split('\n').filter { it.isNotEmpty() } }

    override fun setParams(p: SceneParams) {
        guard.use(Unit) { h ->
            SceneParamsCodec.pack(p, paramFrame)
            GeodeNative.vizSetParams(h, paramFrame)
        }
    }

    override fun setReducedMotion(on: Boolean) {
        guard.use(Unit) { h -> if (sent.reducedMotion(on)) GeodeNative.vizSetReducedMotion(h, on) }
    }

    override fun setTransition(
        id: String,
        durationMs: Long,
    ) {
        guard.use(Unit) { h -> if (sent.transition(id, durationMs)) GeodeNative.vizSetTransition(h, id, durationMs) }
    }

    fun beginParamMorph(seconds: Float) {
        guard.use(Unit) { h -> GeodeNative.vizBeginParamMorph(h, seconds) }
    }

    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizSetTouch(h, xy, n) }
    }

    fun queueTouchStroke(
        nx: Float,
        ny: Float,
        ndx: Float,
        ndy: Float,
        dt: Float,
        strength: Float,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizQueueTouchStroke(h, nx, ny, ndx, ndy, dt, strength) }
    }

    fun setFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizSetFluidInjection(h, force, dye) }
    }

    fun loadMilkPreset(path: String) {
        guard.use(Unit) { h -> GeodeNative.vizLoadMilkPreset(h, path) }
    }

    fun reloadMilkPreset() {
        guard.use(Unit) { h -> GeodeNative.vizReloadMilkPreset(h) }
    }

    /** The preset MilkDrop last compiled, handed out once; null until the next one. */
    fun takeMilkPresetLoaded(): String? = guard.use(null) { h -> GeodeNative.vizTakeMilkPresetLoaded(h) }

    /** The director's new-section and drop decisions since the last call (one Auto mode). */
    fun takeDirectorStyleMoments(): Int = guard.use(0) { h -> GeodeNative.vizTakeDirectorStyleMoments(h) }

    /** The renderer's sound engine: interleaved PCM in play order, each sample once, before [render]. */
    fun writeAudio(
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRateHz: Int,
    ) {
        if (frames > 0) guard.use(Unit) { h -> GeodeNative.vizWriteAudio(h, interleaved, frames, channels, sampleRateHz) }
    }

    fun resetAudio() {
        guard.use(Unit) { h -> GeodeNative.vizResetAudio(h) }
    }

    /** Settings › Audio, as [dev.geode.engine.audio.SoundTuning.toFloats]. */
    override fun setSoundTuning(floats: FloatArray) {
        guard.use(Unit) { h -> GeodeNative.vizSetSoundTuning(h, floats) }
    }

    override fun setLfoConfigs(configs: List<LfoConfig>) {
        guard.use(Unit) { h ->
            configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(h, slot, ModConfigCodec.packLfo(c)) }
        }
    }

    override fun setAdsrConfigs(configs: List<AdsrConfig>) {
        guard.use(Unit) { h ->
            configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(h, slot, ModConfigCodec.packAdsr(c)) }
        }
    }

    /** The user's routes and hand mask, sent only when [routes] (by identity) or [mask] changes; native resolves what plays. */
    override fun setUserReactions(
        routes: List<ReactionRoute>?,
        mask: Int,
    ) {
        guard.use(Unit) { h ->
            if (sent.userReactions(routes, mask)) {
                GeodeNative.vizSetUserReactions(h, routes?.let { BuildStyleReactionsUseCase.packExact(it) }, mask)
            }
        }
    }

    fun gyroSample(
        azimuth: Float,
        pitch: Float,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizGyroSample(h, azimuth, pitch) }
    }

    fun gyroReset() {
        guard.use(Unit) { h -> GeodeNative.vizGyroReset(h) }
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
    ): Int =
        guard.use(0) { h ->
            GeodeNative.vizFrameBegin(
                h,
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
    ): Int = guard.use(0) { h -> GeodeNative.vizPinEngineSettings(h, entitled, durationMs) }

    fun setOffscreen(on: Boolean) {
        guard.use(Unit) { h -> GeodeNative.vizSetOffscreen(h, on) }
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
        guard.use(Unit) { h -> GeodeNative.vizPrepareOffscreen(h) }
    }

    /** See [GeodeNative.vizSetFrameRate]. A no-op before the handle exists. */
    fun setFrameRate(fps: Int) {
        guard.use(Unit) { h -> GeodeNative.vizSetFrameRate(h, fps) }
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
        guard.use(Unit) { h -> GeodeNative.vizSetEdgeGlow(h, edgeGlow) }
    }

    private fun applyUiBlur() {
        guard.use(Unit) { h -> GeodeNative.vizSetUiBlurRegion(h, uiBlur[0], uiBlur[1], uiBlur[2], uiBlur[3], uiBlur[4], uiBlur[5]) }
    }

    fun surfaceCreated() {
        // A new surface may be a new GL context: send every value once more, as the renderer does.
        sent.clear()
        if (create()) guard.use(Unit) { h -> GeodeNative.vizSurfaceCreated(h) }
    }

    fun surfaceChanged(
        width: Int,
        height: Int,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizSurfaceChanged(h, width, height) }
    }

    override fun setScene(sceneId: String): Boolean = guard.use(false) { h -> GeodeNative.vizSetScene(h, sceneId) }

    /** An export segment's style switches; see [GeodeNative.vizSetStyleSchedule]. */
    fun setStyleSchedule(schedule: StyleSchedule) {
        if (schedule.isEmpty) return
        guard.use(Unit) { h ->
            GeodeNative.vizSetStyleSchedule(
                h,
                schedule.cues.map { it.atMs.toDouble() }.toDoubleArray(),
                schedule.cues.map { it.sceneId }.toTypedArray(),
                schedule.cues.map { it.milkPath.orEmpty() }.toTypedArray(),
                schedule.beatCutIds.toTypedArray(),
                schedule.everyBeats,
                schedule.lockedStyleIds.toTypedArray(),
            )
        }
    }

    fun warmTransition(id: String) {
        guard.use(Unit) { h -> GeodeNative.vizWarmTransition(h, id) }
    }

    fun cut() {
        guard.use(Unit) { h -> GeodeNative.vizCut(h) }
    }

    fun render(
        timeSeconds: Double,
        targetFbo: Int,
    ) {
        guard.use(Unit) { h -> GeodeNative.vizRender(h, timeSeconds, targetFbo) }
    }

    fun releaseScenes() {
        guard.use(Unit) { h -> GeodeNative.vizReleaseScenes(h) }
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
        guard.use(Unit) { h -> GeodeNative.vizSetUnderlay(h, pixels, width, height, blend, amount) }
    }

    /** Reports the native error state once per change: a message, or null when it clears. */
    fun pollError(onChange: (String?) -> Unit) {
        val error = guard.use(null) { h -> GeodeNative.vizLastError(h) } ?: return
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

/**
 * The native handle and the one rule that keeps it alive: a call runs under the read lock with
 * the handle read inside it, and [destroy] frees under the write lock, so a free waits for every
 * call in flight and a call that starts after it sees 0 and returns its default (#853). The GL
 * thread's per-frame calls take the uncontended read side only (no extra JNI, no blocking); the
 * write side is taken at create and destroy, once per attach. [create]'s fast path (already
 * created) is a volatile read, so the main thread never waits on a frame.
 *
 * Re-entrant: [create]'s init may call [use] (the write holder may take the read lock). Never call
 * [create] from inside [use]: a read lock cannot be upgraded.
 */
internal class HandleGuard {
    @PublishedApi
    internal val lock = ReentrantReadWriteLock()

    @Volatile
    @PublishedApi
    internal var raw = 0L

    inline fun <T> use(
        default: T,
        block: (Long) -> T,
    ): T {
        if (raw == 0L) return default
        val read = lock.readLock()
        read.lock()
        try {
            val h = raw
            return if (h == 0L) default else block(h)
        } finally {
            read.unlock()
        }
    }

    /** True once a handle exists. [make] returns 0 on failure; [init] runs once per new handle. */
    fun create(
        make: () -> Long,
        init: (Long) -> Unit,
    ): Boolean {
        if (raw != 0L) return true
        val write = lock.writeLock()
        write.lock()
        try {
            if (raw == 0L) {
                val h = make()
                raw = h
                if (h != 0L) init(h)
            }
            return raw != 0L
        } finally {
            write.unlock()
        }
    }

    fun destroy(free: (Long) -> Unit) {
        val write = lock.writeLock()
        write.lock()
        try {
            val h = raw
            if (h == 0L) return
            raw = 0L
            free(h)
        } finally {
            write.unlock()
        }
    }
}
