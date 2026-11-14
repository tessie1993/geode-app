package dev.geode.viz

import android.content.Context
import android.content.res.AssetManager
import dev.geode.data.LookSnapshot
import dev.geode.data.LookUnderlay
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.scene.SceneParams
import java.io.File
import java.lang.ref.Cleaner

/**
 * Owns one `geode_viz` handle: the native renderer behind a GL surface or an offscreen target.
 *
 * Every call is a no-op until [create] (or [surfaceCreated]) has run; the GL-thread rules of the
 * C API apply unchanged. The look and Settings › Performance are pushed from any thread into the
 * handle's own latches, which keep what changed; a live frame is then one [frame] call.
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

    private val paramFrame = FloatArray(SceneParams.FIELDS)

    // What the owner last set, kept for the next handle: a handle is destroyed on every detach, and the new
    // renderer knows none of it. Guarded by this object's monitor, like the handle's create and destroy.
    private var look: LookSnapshot? = null
    private var sentUnderlay: LookUnderlay? = null
    private var underlaySent = false
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
                underlaySent = false
                look?.let(::pushLook)
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
     * The whole look crosses; native passes on only the parts that differ from the look this handle was set
     * last. The scene is a request the next live frame applies once the renderer can draw it.
     */
    @Synchronized
    fun setLook(next: LookSnapshot) {
        look = next
        if (handle != 0L) pushLook(next)
    }

    private fun pushLook(l: LookSnapshot) {
        l.params.frameInto(paramFrame)
        GeodeNative.vizSetLook(
            handle,
            l.sceneId,
            paramFrame,
            l.lfos,
            l.adsrs,
            l.soundTuning?.toFloatArray(),
            l.reactions,
            l.reducedMotion,
            l.transitionId,
            l.transitionMs,
        )
        // The picture is megabytes, so it crosses only as a new object, as the stores make one for a change.
        val u = l.underlay
        if (!underlaySent || u !== sentUnderlay) {
            setUnderlay(u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
            sentUnderlay = u
            underlaySent = true
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
        p.frameInto(paramFrame)
        GeodeNative.vizSetParams(handle, paramFrame)
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
     * each sample once (0: nothing, and the last ring goes), and [fps] and [capFps] are the rate the surface last
     * drew at and its own cap, which native keeps apart for the thermal step. The `FRAME_*` events; 0 before the
     * handle exists. GL thread.
     */
    fun frame(
        ring: Long,
        fps: Float,
        capFps: Float,
    ): Int = if (handle == 0L) 0 else GeodeNative.vizFrame(handle, ring, fps, capFps)

    /** Settings › Audio's saved tuning, null for the defaults; native resolves and clamps it. */
    fun setSoundTuning(saved: List<Float>?) {
        if (handle != 0L) GeodeNative.vizSetSoundTuning(handle, saved?.toFloatArray())
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
    private fun setUnderlay(
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    ) {
        if (handle != 0L) GeodeNative.vizSetUnderlay(handle, pixels, width, height, blend, amount)
    }

    /** The native error, null when clean. A live frame says when it changed (`FRAME_ERROR_CHANGED`). */
    fun lastError(): String? = if (handle == 0L) null else GeodeNative.vizLastError(handle).ifEmpty { null }

    private companion object {
        /** One daemon thread for every renderer in the process. */
        val CLEANER: Cleaner = Cleaner.create()
    }
}
