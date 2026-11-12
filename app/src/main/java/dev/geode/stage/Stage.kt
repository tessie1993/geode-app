package dev.geode.stage

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.Surface
import dev.geode.data.MilkPathPrefs
import dev.geode.engine.audio.SampleRing
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.render.BlendMode
import dev.geode.render.PerformanceSettings
import dev.geode.render.SceneFactory
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.look.LookSnapshot
import dev.geode.render.look.LookUnderlay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.lang.ref.Cleaner
import java.nio.ByteBuffer

/**
 * One native stage (`core/stage`): a surface's render thread, EGL context, vsync and renderer, behind one
 * handle. This hands values across and reads events back; every rule (pacing, when it draws, what a look
 * change restarts) is native.
 *
 * Native never calls up, so the events are polled on the main thread, once per display frame while the
 * stage draws. Every callback therefore runs on the main thread.
 */
class Stage(
    context: Context,
    kind: Int,
    capFps: Float,
) : Choreographer.FrameCallback {
    private val appContext = context.applicationContext

    // Kept referenced for as long as the native AAssetManager resolved from it may be used.
    private val assets = appContext.assets
    private val prefs = appContext.geodeContainer.prefsFiles.viz
    private val main = Handler(Looper.getMainLooper())

    // The Cleaner's copy of the handle: a stage nobody closed still ends its thread and renderer once this
    // object is unreachable.
    private val owned =
        Owned(
            GeodeNative.stageCreate(
                assets,
                appContext.cacheDir.absolutePath,
                File(appContext.filesDir, "milk/textures").absolutePath,
                kind,
                capFps,
            ),
        )

    @Volatile
    private var handle = owned.handle

    /** What this stage draws; native passes on only the parts that differ from the look set last. */
    @Volatile
    var look: LookSnapshot = LookSnapshot()
        set(value) {
            field = value
            pushLook(value)
        }

    /** Settings › Performance's quality. The fps cap is [capFps]. */
    @Volatile
    var performance: PerformanceSettings = PerformanceSettings.HIGH
        set(value) {
            field = value
            GeodeNative.stageSetQuality(handle, value.renderScale, value.marchScale, value.postFx, value.adaptive, value.liquidHot.native)
        }

    /** The surface's own fps cap; 0 or less is none. */
    @Volatile
    var capFps: Float = capFps
        set(value) {
            field = value
            GeodeNative.stageSetCap(handle, value)
        }

    var onShaderError: (String?) -> Unit = {}
    var onMilkPresetLoaded: (String) -> Unit = {}

    /** The auto-director chose a new look at a new section or a drop; the app's Auto changes style on it. */
    var onDirectorStyleMoment: () -> Unit = {}

    /** Lets the UI re-crop the background image to the surface's new pixel size. */
    var onSurfaceSizeChanged: (width: Int, height: Int) -> Unit = { _, _ -> }

    /** The ring the stage hears, asked every polled frame so a playback session that comes and goes is picked up. */
    var soundInput: () -> SampleRing? = { null }

    /** This stage is the one a cast carries: the app's stage view sets it, never the wallpaper or the dream. */
    var castSource = false
        set(on) {
            field = on
            GeodeNative.stageSetCastSource(handle, on)
        }

    /** The surface in pixels, 0 before the first one: the size Record's clock is made for. */
    @Volatile
    var surfaceWidth = 0
        private set

    @Volatile
    var surfaceHeight = 0
        private set

    private val liquid = MutableStateFlow(0)

    /** What Liquid mode ran last (`liquid::Mode`: 0 off, 1 full, 2 lite, 3 warp, 4 paused); changes only. */
    val liquidMode: StateFlow<Int> = liquid.asStateFlow()

    // Held while native reads it: a ring's handle is only valid while the ring is reachable.
    private var heard: SampleRing? = null
    private var sentUnderlay: LookUnderlay? = null
    private var underlaySent = false
    private var stillThen: ((Bitmap) -> Unit)? = null
    private var posted = false
    private var pollUntilMs = 0L

    init {
        CLEANER.register(this, owned)
        MilkPathPrefs.seed(prefs)
        pushLook(look)
        performance = PerformanceSettings.HIGH
        milkRestorePath()?.let { GeodeNative.stageLoadMilkPreset(handle, it) }
    }

    /** Ends the stage for good: its thread frees the renderer, and every later call does nothing. */
    fun close() {
        val h = handle
        handle = 0L
        owned.handle = 0L
        if (h == 0L) return
        GeodeNative.stageSetSound(h, 0L)
        heard = null
        GeodeNative.stageDestroy(h)
    }

    val milkdropAvailable: Boolean get() = GeodeNative.stageKnows(handle, GeodeNative.STYLE_MILKDROP)

    fun availableSceneIds(): List<String> = GeodeNative.stageSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    /** Process-wide: export, the wallpaper and the dream draw it too (core/viz/ShaderOverrides). */
    fun submitShader(
        sceneId: String,
        fragmentSrc: String,
    ) = GeodeNative.shaderOverrideSet(sceneId, fragmentSrc)

    fun customShaderFor(sceneId: String): String? = GeodeNative.shaderOverride(sceneId)

    /** Customize › Layers, process-wide (core/viz/LayerState). */
    fun submitLayer(
        sceneId: String?,
        mix: Float,
        blend: BlendMode,
    ) = GeodeNative.layerSet(sceneId.orEmpty(), mix, blend.ordinal)

    fun submitFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) = GeodeNative.stageSetFluidInjection(handle, force, dye)

    fun loadMilkPreset(path: String) {
        GeodeNative.milkMemoryRequest(path)
        GeodeNative.stageLoadMilkPreset(handle, path)
        poll()
    }

    fun reloadCurrentMilkPreset() = GeodeNative.stageReloadMilkPreset(handle)

    fun warmTransition(id: String) = GeodeNative.stageWarmTransition(handle, id)

    /** The pointers that are down right now, in y-up NDC; `n = 0` says every finger has lifted. */
    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) = GeodeNative.stageSetTouch(handle, xy, n)

    fun setTouchSmear(
        on: Boolean,
        strength: Float,
    ) = GeodeNative.stageSetTouchSmear(handle, on, strength)

    fun beginParamMorph(seconds: Float) = GeodeNative.stageBeginParamMorph(handle, seconds)

    /** The glass sheet's blur region; see [StageView.setUiBlurRegion]. */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) = GeodeNative.stageSetUiBlurRegion(handle, left, top, right, bottom, strength, cornerPx)

    /** The wallpaper's edge glow options (`core/viz/frame/EdgeGlowPass`). */
    fun setEdgeGlow(options: FloatArray) = GeodeNative.stageSetEdgeGlow(handle, options)

    /** The rate the stage draws at now; 0 every vsync. */
    fun pacedFps(): Float = GeodeNative.stagePacedFps(handle)

    /** Tap along: true on the tap that completes a streak on the beat this stage heard. */
    fun tapAlong(uptimeNs: Long): Boolean = GeodeNative.stageTapAlong(handle, uptimeNs)

    /** The next finished visual (no app UI), handed to [then] on the main thread. */
    fun requestStill(then: (Bitmap) -> Unit) {
        stillThen = then
        GeodeNative.stageStillRequest(handle)
        poll()
    }

    /** The surface from `surfaceCreated`, or null from `surfaceDestroyed`; returns once the stage let go of the old one. */
    fun setWindow(
        surface: Surface?,
        width: Int,
        height: Int,
    ) {
        GeodeNative.stageSetWindow(handle, surface, width, height)
        if (surface != null) sized(width, height)
        poll()
    }

    fun setSize(
        width: Int,
        height: Int,
    ) {
        GeodeNative.stageSetSize(handle, width, height)
        sized(width, height)
        poll()
    }

    /** What the app's view sees of itself, as `GeodeNative.STAGE_VIEW_*` bits. */
    fun setView(flags: Int) {
        GeodeNative.stageSetView(handle, flags)
        poll()
    }

    /** The wallpaper's and the dream's `SURFACE_*` bits; answers the run mode native applied. */
    fun setSurface(
        flags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Int = GeodeNative.stageSetSurface(handle, flags, animateUntilMs, nowMs).also { poll() }

    fun pause() = GeodeNative.stagePause(handle)

    fun resume() {
        GeodeNative.stageResume(handle)
        poll()
    }

    /** `surfaceRedrawNeeded`: one frame now, even while the vsync is stopped. */
    fun redraw() {
        GeodeNative.stageRedraw(handle)
        poll()
    }

    fun gyroListens(wanted: Boolean): Boolean = GeodeNative.stageGyroListens(handle, wanted)

    fun gyroPush(
        x: Float,
        y: Float,
        z: Float,
        w: Float,
        displayRotation: Int,
    ) = GeodeNative.stageGyroPush(handle, x, y, z, w, displayRotation)

    /** Opens Record's tee; when it cannot open, native stops [recorder] as failed. False when this stage is gone. */
    fun recordStart(
        recorder: Long,
        session: Long,
        mark: RecordMark,
    ): Boolean = GeodeNative.stageRecordStart(handle, recorder, session, mark.tile, mark.width, mark.height, mark.placement, mark.corner)

    fun recordStop() = GeodeNative.stageRecordStop(handle)

    /** True when no tee is open or waiting to open, and for a stage that is gone: the recorder and session may be freed. */
    val recordClosed: Boolean get() = GeodeNative.stageRecordClosed(handle)

    fun exportSceneFactory(sceneId: String): SceneFactory {
        val id = sceneId
        val preset = milkRestorePath()
        return object : SceneFactory {
            override val sceneId: String = id

            override val milkPresetPath: String? = preset
        }
    }

    private fun milkRestorePath(): String? = GeodeNative.milkMemoryRestorePath(assets, File(appContext.filesDir, "milk").path)

    private fun sized(
        width: Int,
        height: Int,
    ) {
        surfaceWidth = width
        surfaceHeight = height
        onSurfaceSizeChanged(width, height)
    }

    @Synchronized
    private fun pushLook(l: LookSnapshot) {
        GeodeNative.stageSetLook(
            handle,
            l.sceneId,
            SceneParamsCodec.pack(l.params),
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
            GeodeNative.stageSetUnderlay(handle, u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
            sentUnderlay = u
            underlaySent = true
        }
    }

    /** Keeps the event poll going for a while: what a single frame of a stopped stage says is still read. */
    private fun poll() {
        if (Looper.myLooper() != main.looper) {
            main.post { poll() }
            return
        }
        pollUntilMs = SystemClock.uptimeMillis() + POLL_LINGER_MS
        if (posted || handle == 0L) return
        posted = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        posted = false
        val h = handle
        if (h == 0L) return
        val ring = soundInput()
        if (ring !== heard) {
            GeodeNative.stageSetSound(h, ring?.handle ?: 0L)
            heard = ring
        }
        val events = GeodeNative.stageTakeEvents(h)
        if (events and GeodeNative.FRAME_LIQUID_CHANGED != 0) liquid.value = GeodeNative.stageLiquidMode(h)
        if (events and GeodeNative.FRAME_ERROR_CHANGED != 0) onShaderError(GeodeNative.stageError(h)?.ifEmpty { null })
        if (events and GeodeNative.FRAME_MILK_LOADED != 0) milkLoaded(h)
        if (events and GeodeNative.FRAME_DIRECTOR_MOMENT != 0 && GeodeNative.stageTakeDirectorMoments(h) > 0) onDirectorStyleMoment()
        if (events and GeodeNative.FRAME_STILL_READY != 0) deliverStill(h)
        val draws = GeodeNative.stageState(h) and GeodeNative.STAGE_STATE_VSYNC != 0
        if (draws || SystemClock.uptimeMillis() < pollUntilMs) {
            posted = true
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    private fun milkLoaded(h: Long) {
        while (true) {
            val path = GeodeNative.stageTakeMilkLoaded(h) ?: return
            if (GeodeNative.milkMemoryLoaded(path)) MilkPathPrefs.save(prefs, path)
            onMilkPresetLoaded(path)
        }
    }

    /** The still to whoever asked; one that cannot be read is asked for again. */
    private fun deliverStill(h: Long) {
        val then = stillThen ?: return
        val size = IntArray(2)
        val pixels = if (GeodeNative.stageStillSize(h, size)) ByteBuffer.allocateDirect(size[0] * size[1] * 4) else null
        if (pixels == null || !GeodeNative.stageStillRead(h, pixels)) {
            GeodeNative.stageStillRequest(h)
            return
        }
        stillThen = null
        then(Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888).apply { copyPixelsFromBuffer(pixels) })
    }

    /** Holds no reference to its [Stage], or the Cleaner could never run it. */
    private class Owned(
        @Volatile var handle: Long,
    ) : Runnable {
        override fun run() {
            val h = handle
            handle = 0L
            if (h != 0L) GeodeNative.stageDestroy(h)
        }
    }

    private companion object {
        /** Long enough for a stopped stage's one frame (a redraw, a still) to finish and be read. */
        const val POLL_LINGER_MS = 5_000L

        /** One daemon thread for every stage in the process. */
        val CLEANER: Cleaner = Cleaner.create()
    }
}
