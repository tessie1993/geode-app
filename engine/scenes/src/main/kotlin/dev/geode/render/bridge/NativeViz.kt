package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.AdsrEngine
import dev.geode.render.LfoConfig
import dev.geode.render.LfoEngine
import dev.geode.render.PerformanceSettings
import dev.geode.render.ReactionRoute
import dev.geode.render.look.LookSnapshot
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.SceneParams
import java.io.File
import java.lang.ref.Cleaner
import java.lang.ref.Reference
import java.nio.ByteBuffer

/**
 * Owns one `geode_viz` handle: the native renderer behind a GL surface or an offscreen target.
 *
 * Every call is a no-op until [create] (or [surfaceCreated]) has run; the GL-thread rules of the
 * C API apply unchanged. The look and Settings › Performance are pushed when they change, from any
 * thread, into the handle's own latches; a live frame is then one [frame] call.
 */
class NativeViz(
    context: Context,
) {
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

    /** The `geode_viz` handle for the native export job, which drives this renderer itself; 0 before [create]. */
    val nativeHandle: Long get() = handle

    // The handle again, for the Cleaner (#999). A teardown queues the release on the GL thread and waits
    // 200 ms (VisualizerView, the wallpaper engine); when the wait runs out, the queued job dies with the
    // thread and nothing else destroyed the native renderer: its scenes, sound engine and projectM heap
    // leaked for the life of the process. Once this object is unreachable the Cleaner destroys what is
    // left. JNI's destroy waits for calls in flight and ignores a handle already gone; without a context
    // its GL deletes are no-ops, and the lost context took those objects with it.
    private val owned = Owned()

    init {
        CLEANER.register(this, owned)
    }

    private val paramFrame = FloatArray(SceneParamsCodec.FIELDS)
    private var reportedError = ""

    // What the owner last set, kept for the next handle: a handle is destroyed on every detach, and the new
    // renderer knows none of it. Guarded by this object's monitor, like the handle's create and destroy.
    private var look: LookSnapshot? = null
    private var quality: PerformanceSettings? = null

    // Shake to screenshot's waiting request: set from any thread, answered on the GL thread after a frame.
    @Volatile
    private var stillThen: ((Bitmap) -> Unit)? = null

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
            owned.handle = handle
            if (handle != 0L) {
                GeodeNative.vizSetMilkTextureDir(handle, milkTextureDir)
                applyUiBlur()
                edgeGlow?.let { GeodeNative.vizSetEdgeGlow(handle, it) }
                if (stillThen != null) GeodeNative.vizStillRequest(handle)
                if (castSource) GeodeNative.vizSetCastSource(handle, true)
                if (smearOn) GeodeNative.vizSetTouchSmear(handle, true, smearStrength)
                quality?.let(::pushQuality)
                look?.let { pushLook(it, was = null) }
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
        owned.handle = 0L
    }

    /** Holds no reference to its [NativeViz], or the Cleaner could never run it. */
    private class Owned : Runnable {
        @Volatile
        var handle = 0L

        override fun run() {
            val h = handle
            handle = 0L
            if (h != 0L) GeodeNative.vizDestroy(h)
        }
    }

    fun knows(sceneId: String): Boolean = handle != 0L && GeodeNative.vizKnows(handle, sceneId)

    fun sceneIds(): List<String> = if (handle == 0L) emptyList() else GeodeNative.vizSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    /**
     * What this renderer draws, from any thread, kept for the next handle ([create] gives it the whole look).
     * Only the parts that are new objects cross JNI, each into one of the handle's any-thread latches: the
     * stores make a new instance for a change, so an unrelated edit costs nothing. The scene is a request the
     * next live frame applies once the renderer can draw it.
     */
    @Synchronized
    fun setLook(next: LookSnapshot) {
        val was = look
        look = next
        if (handle != 0L) pushLook(next, was)
    }

    private fun pushLook(
        l: LookSnapshot,
        was: LookSnapshot?,
    ) {
        if (l.sceneId != was?.sceneId) GeodeNative.vizRequestScene(handle, l.sceneId)
        if (l.params !== was?.params) setParams(l.params)
        if (l.lfos !== was?.lfos) setLfoConfigs(l.lfos)
        if (l.adsrs !== was?.adsrs) setAdsrConfigs(l.adsrs)
        if (was == null || l.soundTuning !== was.soundTuning) setSoundTuning(l.soundTuning)
        if (was == null || l.reactions !== was.reactions) setUserReactions(l.reactions)
        if (l.reducedMotion != was?.reducedMotion) setReducedMotion(l.reducedMotion)
        if (l.transitionId != was?.transitionId || l.transitionMs != was?.transitionMs) setTransition(l.transitionId, l.transitionMs)
        if (was == null || l.underlay !== was.underlay) {
            val u = l.underlay
            setUnderlay(u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
        }
    }

    /** Settings › Performance for a live surface, from any thread; kept for the next handle. An export never sets it. */
    @Synchronized
    fun setQuality(settings: PerformanceSettings) {
        quality = settings
        if (handle != 0L) pushQuality(settings)
    }

    private fun pushQuality(s: PerformanceSettings) =
        GeodeNative.vizSetQuality(handle, s.renderScale, s.marchScale, s.postFx, s.adaptive, s.liquidHot.native)

    @Synchronized
    fun setParams(p: SceneParams) {
        if (handle == 0L) return
        SceneParamsCodec.pack(p, paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
    }

    fun setReducedMotion(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetReducedMotion(handle, on)
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

    /** Kept for a new handle, like the edge glow: the user's smear switch outlives a detach. */
    fun setTouchSmear(
        on: Boolean,
        strength: Float,
    ) {
        smearOn = on
        smearStrength = strength
        if (handle != 0L) GeodeNative.vizSetTouchSmear(handle, on, strength)
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
     * An export's track audio: the native feed at [feed] hands this renderer's sound engine its held frames
     * up to the feed's target, with no copy through Kotlin (to nobody once destroyed). True once the target is
     * reached.
     */
    fun hearTrack(feed: Long): Boolean = GeodeNative.vizHearTrack(handle, feed)

    /** After the pre-roll, before frame 0 (core/output/PreRoll); false when the engine heard nothing. */
    fun endPreRoll(rangeStartMs: Long): Boolean = GeodeNative.vizEndPreRoll(handle, rangeStartMs)

    /**
     * One live frame into framebuffer 0 ([GeodeNative.vizFrame]): the renderer hears what [ring] gained, natively,
     * each sample once (null: nothing, and the last ring goes), and [fps] and [capFps] are the rate the surface last
     * drew at and its own cap, which native keeps apart for the thermal step. The `FRAME_*` events; 0 before the
     * handle exists. GL thread.
     */
    fun frame(
        ring: SampleRing?,
        fps: Float,
        capFps: Float,
    ): Int {
        if (handle == 0L) return 0
        val events = GeodeNative.vizFrame(handle, ring?.handle ?: 0L, fps, capFps)
        // The ring's handle is only valid while the ring is held: hold it past the call.
        Reference.reachabilityFence(ring)
        return events
    }

    /** What Liquid mode ran when a frame last said `FRAME_LIQUID_CHANGED` (`liquid::Mode`). */
    fun liquidMode(): Int = if (handle == 0L) 0 else GeodeNative.vizLiquidMode(handle)

    /** Settings › Audio's saved tuning, null for the defaults; native resolves and clamps it. */
    fun setSoundTuning(saved: List<Float>?) {
        if (handle != 0L) GeodeNative.vizSetSoundTuning(handle, saved?.toFloatArray())
    }

    fun setLfoConfigs(configs: List<LfoConfig>) {
        if (handle == 0L) return
        configs.take(LfoEngine.SLOTS).forEachIndexed { slot, c -> GeodeNative.vizSetLfo(handle, slot, ModConfigCodec.packLfo(c)) }
    }

    fun setAdsrConfigs(configs: List<AdsrConfig>) {
        if (handle == 0L) return
        configs.take(AdsrEngine.COUNT).forEachIndexed { slot, c -> GeodeNative.vizSetAdsr(handle, slot, ModConfigCodec.packAdsr(c)) }
    }

    /** The user's routes (null: the style's DNA); native merges them under the hand mask it reads itself. */
    fun setUserReactions(routes: List<ReactionRoute>?) {
        if (handle != 0L) GeodeNative.vizSetUserReactions(handle, routes?.let { BuildStyleReactionsUseCase.packExact(it) })
    }

    /** [dev.geode.render.GyroLook]'s reading: the rotation vector's x, y, z, w and the display rotation. Any thread. */
    fun gyroPush(
        x: Float,
        y: Float,
        z: Float,
        w: Float,
        displayRotation: Int,
    ) {
        if (handle != 0L) GeodeNative.vizGyroPush(handle, x, y, z, w, displayRotation)
    }

    /** The sensor stopped: the next frame recentres the look. Any thread. */
    fun gyroOff() {
        if (handle != 0L) GeodeNative.vizGyroOff(handle)
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
    private var smearOn = false
    private var smearStrength = 1f

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

    /**
     * This renderer is the stage a cast carries ([GeodeNative.vizSetCastSource]): the app's one stage view sets it,
     * never the wallpaper, the dream or an export. Any thread; latched for every new handle.
     */
    @Volatile
    var castSource = false
        @Synchronized set(on) {
            field = on
            if (handle != 0L) GeodeNative.vizSetCastSource(handle, on)
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

    /** GL thread. Record's tee into [session]'s encoder; see [GeodeNative.recordSessionTeeStart]. */
    fun recordStart(
        session: Long,
        watermark: Long,
    ): Boolean = handle != 0L && GeodeNative.recordSessionTeeStart(session, handle, watermark)

    fun recordStop() {
        if (handle != 0L) GeodeNative.vizRecordStop(handle)
    }

    /** A new surface keeps what was pushed: the native handle holds it, so nothing is sent again here. */
    fun surfaceCreated() {
        if (create()) GeodeNative.vizSurfaceCreated(handle)
    }

    fun surfaceChanged(
        width: Int,
        height: Int,
    ) {
        if (handle != 0L) GeodeNative.vizSurfaceChanged(handle, width, height)
    }

    /** GL thread, at once: an export's (and a test's) scene. A live surface's goes with its look. */
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
        if (handle == 0L) return
        GeodeNative.vizRender(handle, timeSeconds, targetFbo)
    }

    /** The next finished visual (no app UI), handed to [then] on the GL thread. Any thread. */
    fun requestStill(then: (Bitmap) -> Unit) {
        stillThen = then
        if (handle != 0L) GeodeNative.vizStillRequest(handle)
    }

    /** Tap along: true on the tap that completes a streak on the beat this renderer heard ([GeodeNative.vizTapAlong]). Any thread. */
    fun tapAlong(uptimeNs: Long): Boolean = handle != 0L && GeodeNative.vizTapAlong(handle, uptimeNs)

    /** On a frame's `FRAME_STILL_READY`, GL thread: the still to whoever asked; one that cannot be read is asked for again. */
    fun deliverStill() {
        val then = stillThen ?: return
        val size = IntArray(2)
        val pixels = if (GeodeNative.vizStillSize(handle, size)) ByteBuffer.allocateDirect(size[0] * size[1] * 4) else null
        if (pixels == null || !GeodeNative.vizStillRead(handle, pixels)) {
            GeodeNative.vizStillRequest(handle)
            return
        }
        stillThen = null
        then(Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888).apply { copyPixelsFromBuffer(pixels) })
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
        const val UI_BLUR_FLOATS = 6

        /** One daemon thread for every renderer in the process. */
        val CLEANER: Cleaner = Cleaner.create()
    }
}
