package dev.geode.render.bridge

import android.content.Context
import android.content.res.AssetManager
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.look.LookSnapshot
import dev.geode.render.scene.SceneParams
import java.io.File
import java.lang.ref.Cleaner
import java.lang.ref.Reference

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

    // The handle again, for the Cleaner (#999): a renderer nobody destroyed would keep its scenes, sound
    // engine and projectM heap for the life of the process. Once this object is unreachable the Cleaner
    // destroys what is left. JNI's destroy waits for calls in flight and ignores a handle already gone; without a context
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
    private var quality: () -> Unit = {}

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
                quality()
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
        if (l.lfos != was?.lfos) setLfoConfigs(l.lfos)
        if (l.adsrs != was?.adsrs) setAdsrConfigs(l.adsrs)
        if (was == null || l.soundTuning !== was.soundTuning) setSoundTuning(l.soundTuning)
        if (was == null || l.reactions != was.reactions) setUserReactions(l.reactions)
        if (l.reducedMotion != was?.reducedMotion) setReducedMotion(l.reducedMotion)
        if (l.transitionId != was?.transitionId || l.transitionMs != was?.transitionMs) setTransition(l.transitionId, l.transitionMs)
        if (was == null || l.underlay !== was.underlay) {
            val u = l.underlay
            setUnderlay(u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
        }
    }

    /** Settings › Performance for a live surface, from any thread; kept for the next handle. An export never sets it. */
    @Synchronized
    fun setQuality(
        renderScale: Float,
        marchScale: Float,
        postFx: Boolean,
        adaptive: Boolean,
        liquidHotPolicy: Int,
    ) {
        quality = { if (handle != 0L) GeodeNative.vizSetQuality(handle, renderScale, marchScale, postFx, adaptive, liquidHotPolicy) }
        quality()
    }

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

    fun loadMilkPreset(path: String) {
        if (handle != 0L) GeodeNative.vizLoadMilkPreset(handle, path)
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

    /** Settings › Audio's saved tuning, null for the defaults; native resolves and clamps it. */
    fun setSoundTuning(saved: List<Float>?) {
        if (handle != 0L) GeodeNative.vizSetSoundTuning(handle, saved?.toFloatArray())
    }

    fun setLfoConfigs(lfos: String) {
        if (handle != 0L) GeodeNative.vizSetLfos(handle, lfos)
    }

    fun setAdsrConfigs(adsrs: String) {
        if (handle != 0L) GeodeNative.vizSetAdsrs(handle, adsrs)
    }

    /** The user's saved routes (null: the style's DNA); native merges them under the hand mask it reads itself. */
    fun setUserReactions(routes: String?) {
        if (handle != 0L) GeodeNative.vizSetReactions(handle, routes)
    }

    fun setOffscreen(on: Boolean) {
        if (handle != 0L) GeodeNative.vizSetOffscreen(handle, on)
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

    fun render(
        timeSeconds: Double,
        targetFbo: Int,
    ) {
        if (handle == 0L) return
        GeodeNative.vizRender(handle, timeSeconds, targetFbo)
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
        /** One daemon thread for every renderer in the process. */
        val CLEANER: Cleaner = Cleaner.create()
    }
}
