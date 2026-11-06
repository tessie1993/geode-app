package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
import android.service.dreams.DreamService
import android.service.wallpaper.WallpaperService
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.SceneParams
import java.io.File

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
    private val milkTextureDir: String = File(context.filesDir, "milk/textures").absolutePath

    // Which Android surface this is, known only by the Context it was built with; native decides what
    // follows from it (`geode_viz_set_surface_kind`). An export says so in [setOffscreen].
    private val surfaceKind: Int =
        when (context) {
            is WallpaperService -> SURFACE_WALLPAPER
            is DreamService -> SURFACE_DREAM
            else -> SURFACE_LIVE
        }

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
            verifyLayouts()
            handle = GeodeNative.vizCreate(assets, cacheDir)
            sent.clear()
            if (handle != 0L) {
                GeodeNative.vizSetMilkTextureDir(handle, milkTextureDir)
                GeodeNative.vizSetSurfaceKind(handle, surfaceKind)
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

    fun setParams(p: SceneParams) {
        if (handle == 0L) return
        SceneParamsCodec.pack(p, paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
    }

    fun setReducedMotion(on: Boolean) {
        if (handle != 0L && sent.reducedMotion(on)) GeodeNative.vizSetReducedMotion(handle, on)
    }

    /**
     * Generators (#471): [slot] 0 sky, 1 wall, 2 material, 3 camera plane; null or "" empties it.
     * Customize › Environment calls these and [generatorText] through `VisualizerRenderer.nativeViz`
     * inside `VisualizerView.queueEvent`, so a call never meets a handle the GL thread is destroying.
     */
    fun setGenerator(
        slot: Int,
        styleId: String?,
    ) {
        if (handle != 0L) GeodeNative.vizSetGenerator(handle, slot, styleId ?: "")
    }

    fun setGeneratorPin(
        slot: Int,
        pinned: Boolean,
    ) {
        if (handle != 0L) GeodeNative.vizSetGeneratorPin(handle, slot, pinned)
    }

    fun setGeneratorPolicy(policy: Int) {
        if (handle != 0L) GeodeNative.vizSetGeneratorPolicy(handle, policy)
    }

    /** The native assignment text (`geode_viz_generator_text`); "" before [create]. */
    fun generatorText(): String = if (handle == 0L) "" else GeodeNative.vizGeneratorText(handle)

    fun setTransition(
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

    fun resetAudio() {
        if (handle != 0L) GeodeNative.vizResetAudio(handle)
    }

    /** Settings › Audio, as [dev.geode.engine.audio.SoundTuning.toFloats]. */
    fun setSoundTuning(floats: FloatArray) {
        if (handle != 0L) GeodeNative.vizSetSoundTuning(handle, floats)
    }

    fun pushPcm(
        mono: FloatArray,
        count: Int,
    ) {
        if (handle != 0L && count > 0) GeodeNative.vizPushPcm(handle, mono, count)
    }

    fun setLfoConfigs(configs: List<LfoConfig>) {
        if (handle == 0L) return
        configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(handle, slot, ModConfigCodec.packLfo(c)) }
    }

    fun setAdsrConfigs(configs: List<AdsrConfig>) {
        if (handle == 0L) return
        configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(handle, slot, ModConfigCodec.packAdsr(c)) }
    }

    fun setReactions(routes: List<ReactionRoute>) {
        if (handle != 0L) GeodeNative.vizSetReactions(handle, ModConfigCodec.packReactions(routes))
    }

    fun setLook(
        yaw: Float,
        pitch: Float,
    ) {
        if (handle != 0L && sent.look(yaw, pitch)) GeodeNative.vizSetLook(handle, yaw, pitch)
    }

    fun setThermal(
        platformStatus: Int,
        headroom: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetThermal(handle, platformStatus, headroom)
    }

    fun setPacedFps(fps: Float) {
        if (handle != 0L && sent.pacedFps(fps)) GeodeNative.vizSetPacedFps(handle, fps)
    }

    fun setQuality(
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
    ) {
        if (handle != 0L) GeodeNative.vizSetQuality(handle, renderScale, marchScale, postFx, adaptive)
    }

    /** Liquid on a hot phone; see [GeodeNative.vizSetLiquidHotPolicy]. */
    fun setLiquidHotPolicy(policy: Int) {
        if (handle != 0L) GeodeNative.vizSetLiquidHotPolicy(handle, policy)
    }

    /** What Liquid mode ran last frame (0 off, 1 full, 2 lite, 3 warp, 4 paused); 0 before the handle exists. */
    fun liquidMode(): Int = if (handle != 0L) GeodeNative.vizLiquidMode(handle) else 0

    /** Pins this renderer's auto-director; see [GeodeNative.vizSetDirector]. */
    fun setDirector(
        amount: Float,
        seed: Long,
    ) {
        if (handle != 0L) GeodeNative.vizSetDirector(handle, amount, seed)
    }

    fun setOffscreen(on: Boolean) {
        if (handle == 0L) return
        GeodeNative.vizSetOffscreen(handle, on)
        GeodeNative.vizSetSurfaceKind(handle, if (on) SURFACE_EXPORT else surfaceKind)
    }

    /** See [GeodeNative.vizSetFrameRate]. A no-op before the handle exists. */
    fun setFrameRate(fps: Int) {
        if (handle != 0L) GeodeNative.vizSetFrameRate(handle, fps)
    }

    /** The glass sheet's blur region; see [GeodeNative.vizSetUiBlurRegion]. A no-op before the handle exists. */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetUiBlurRegion(handle, left, top, right, bottom, strength, cornerPx)
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

    fun setScene(sceneId: String): Boolean = handle != 0L && GeodeNative.vizSetScene(handle, sceneId)

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

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: blend 0 = replace scene where scene is
     * black (screen), 1 = multiply, 2 = add; amount 0..1. null clears. Any thread; latched.
     */
    fun setUnderlay(
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
        // scene::camera::Surface ordinals (Group E's frame::Surface order).
        const val SURFACE_LIVE = 0
        const val SURFACE_WALLPAPER = 1
        const val SURFACE_DREAM = 2
        const val SURFACE_EXPORT = 3

        @Volatile
        private var verified = false

        fun verifyLayouts() {
            if (verified) return
            SceneParamsCodec.verify(GeodeNative.vizParamNames())
            verified = true
        }
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

    fun clear() {
        yaw = Float.NaN
        pitch = Float.NaN
        reduced = null
        transitionId = null
        transitionMs = -1L
        fps = Float.NaN
    }
}
