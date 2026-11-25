package dev.geode.stage

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.os.MessageQueue
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.Surface
import dev.geode.data.LookSnapshot
import dev.geode.data.LookUnderlay
import dev.geode.data.PerformanceSettings
import dev.geode.engine.bridge.DirectorNative
import dev.geode.engine.bridge.StageNative
import dev.geode.export.SceneFactory
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.lang.ref.Cleaner
import java.nio.ByteBuffer

/**
 * One native stage (`core/stage`): a surface's render thread, EGL context, vsync and renderer, behind one
 * handle. This hands values across and reads events back; every rule (pacing, when it draws, what a look
 * change restarts) is native.
 *
 * Native never calls up. The stage hands the main looper a descriptor that is readable while frame events wait, so
 * the main thread wakes for an event, never for a vsync that has none; the sound ring is looked at on a timer.
 * Every callback therefore runs on the main thread.
 */
class Stage(
    context: Context,
    kind: Int,
    capFps: Float,
) {
    private val appContext = context.applicationContext

    // Kept referenced for as long as the native AAssetManager resolved from it may be used.
    private val assets = appContext.assets
    private val main = Handler(Looper.getMainLooper())

    // The Cleaner's copy of the handle: a stage nobody closed still ends its thread and renderer once this
    // object is unreachable.
    private val owned =
        Owned(
            StageNative.stageCreate(
                assets,
                appContext.cacheDir.absolutePath,
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
            if (value == field) return
            field = value
            pushLook(value)
        }

    /** Settings › Performance's quality. The fps cap is [capFps]. */
    @Volatile
    var performance: PerformanceSettings = PerformanceSettings.HIGH
        set(value) {
            field = value
            StageNative.stageSetQuality(handle, value.renderScale, value.marchScale, value.postFx)
            StageNative.stageSetAdaptive(handle, value.adaptive)
        }

    /** The surface's own fps cap; 0 or less is none. */
    @Volatile
    var capFps: Float = capFps
        set(value) {
            field = value
            StageNative.stageSetCap(handle, value)
        }

    var onShaderError: (String?) -> Unit = {}

    /** The auto-director chose a new look at a new section or a drop; the app's Auto changes style on it. */
    var onDirectorStyleMoment: () -> Unit = {}

    /** Lets the UI re-crop the background image to the surface's new pixel size. */
    var onSurfaceSizeChanged: (width: Int, height: Int) -> Unit = { _, _ -> }

    /** The ring handle the stage hears (0: none), asked every polled frame so a playback session that comes and goes is picked up. */
    var soundInput: () -> Long = { 0L }

    /** This stage is the one a cast carries: the app's stage view sets it, never the wallpaper or the dream. */
    var castSource = false
        set(on) {
            field = on
            StageNative.stageSetCastSource(handle, on)
        }

    /** The surface in pixels, 0 before the first one: the size Record's clock is made for. */
    @Volatile
    var surfaceWidth = 0
        private set

    @Volatile
    var surfaceHeight = 0
        private set

    private val liquid = MutableStateFlow(0)

    /** What Liquid mode ran last (`liquid::Mode`: 0 off, 1 full); changes only. */
    val liquidMode: StateFlow<Int> = liquid.asStateFlow()

    // The ring handed to native last.
    private var heard = 0L
    private var sentUnderlay: LookUnderlay? = null
    private var underlaySent = false
    private var stillThen: ((Bitmap) -> Unit)? = null
    private var posted = false
    private var pollUntilMs = 0L
    private var eventFd: ParcelFileDescriptor? = null
    private val ringTick = Runnable { ringPoll() }

    init {
        CLEANER.register(this, owned)
        pushLook(look)
        listenForEvents()
        performance = PerformanceSettings.HIGH
    }

    /** Ends the stage for good: its thread frees the renderer, and every later call does nothing. */
    fun close() {
        val h = handle
        handle = 0L
        main.removeCallbacks(ringTick)
        eventFd?.let {
            main.looper.queue.removeOnFileDescriptorEventListener(it.fileDescriptor)
            it.close()
        }
        eventFd = null
        owned.handle = 0L
        if (h == 0L) return
        StageNative.stageSetSound(h, 0L)
        heard = 0L
        StageNative.stageDestroy(h)
    }

    fun availableSceneIds(): List<String> = StageNative.stageSceneIds(handle).split('\n').filter { it.isNotEmpty() }

    /** Process-wide: export, the wallpaper and the dream draw it too (core/viz/ShaderOverrides). */
    fun submitShader(
        sceneId: String,
        fragmentSrc: String,
    ) = DirectorNative.shaderOverrideSet(sceneId, fragmentSrc)

    fun customShaderFor(sceneId: String): String? = DirectorNative.shaderOverride(sceneId)

    fun submitFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) = StageNative.stageSetFluidInjection(handle, force, dye)

    fun warmTransition(id: String) = StageNative.stageWarmTransition(handle, id)

    /** Plays a response once on this stage's picture (the Song events picker's preview). */
    fun previewResponse(values: FloatArray) = StageNative.stageResponsePreview(handle, values)

    /** The Song event rows' lights and the cost budget as the last frame left them. */
    fun responderStatus(): FloatArray = StageNative.stageResponderStatus(handle)

    /** The pointers that are down right now, in y-up NDC; `n = 0` says every finger has lifted. */
    fun submitTouchPoints(
        xy: FloatArray,
        n: Int,
    ) = StageNative.stageSetTouch(handle, xy, n)

    fun setTouchSmear(
        on: Boolean,
        strength: Float,
    ) = StageNative.stageSetTouchSmear(handle, on, strength)

    fun beginParamMorph(seconds: Float) = StageNative.stageBeginParamMorph(handle, seconds)

    /** The glass sheet's blur region; see [StageView.setUiBlurRegion]. */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) = StageNative.stageSetUiBlurRegion(handle, left, top, right, bottom, strength, cornerPx)

    /** The wallpaper's edge glow options (`core/viz/frame/EdgeGlowPass`). */
    fun setEdgeGlow(options: FloatArray) = StageNative.stageSetEdgeGlow(handle, options)

    /** Tap along: true on the tap that completes a streak on the beat this stage heard. */
    fun tapAlong(uptimeNs: Long): Boolean = StageNative.stageTapAlong(handle, uptimeNs)

    /** The next finished visual (no app UI), handed to [then] on the main thread. */
    fun requestStill(then: (Bitmap) -> Unit) {
        stillThen = then
        StageNative.stageStillRequest(handle)
        poll()
    }

    /** The surface from `surfaceCreated`, or null from `surfaceDestroyed`; returns once the stage let go of the old one. */
    fun setWindow(
        surface: Surface?,
        width: Int,
        height: Int,
    ) {
        StageNative.stageSetWindow(handle, surface, width, height)
        if (surface != null) sized(width, height)
        poll()
    }

    fun setSize(
        width: Int,
        height: Int,
    ) {
        StageNative.stageSetSize(handle, width, height)
        sized(width, height)
        poll()
    }

    /** What the app's view sees of itself, as `StageNative.STAGE_VIEW_*` bits. */
    fun setView(flags: Int) {
        StageNative.stageSetView(handle, flags)
        poll()
    }

    /** The wallpaper's and the dream's `SURFACE_*` bits; answers the run mode native applied. */
    fun setSurface(
        flags: Int,
        animateUntilMs: Long,
        nowMs: Long,
    ): Int = StageNative.stageSetSurface(handle, flags, animateUntilMs, nowMs).also { poll() }

    fun pause() = StageNative.stagePause(handle)

    fun resume() {
        StageNative.stageResume(handle)
        poll()
    }

    /** `surfaceRedrawNeeded`: one frame now, even while the vsync is stopped. */
    fun redraw() {
        StageNative.stageRedraw(handle)
        poll()
    }

    fun gyroListens(wanted: Boolean): Boolean = StageNative.stageGyroListens(handle, wanted)

    fun gyroPush(
        x: Float,
        y: Float,
        z: Float,
        w: Float,
        displayRotation: Int,
    ) = StageNative.stageGyroPush(handle, x, y, z, w, displayRotation)

    /** Opens Record's tee; when it cannot open, native stops [recorder] as failed. False when this stage is gone. */
    fun recordStart(
        recorder: Long,
        session: Long,
        mark: RecordMark,
    ): Boolean = StageNative.stageRecordStart(handle, recorder, session, mark.tile, mark.width, mark.height, mark.placement, mark.corner)

    fun recordStop() = StageNative.stageRecordStop(handle)

    /** True when no tee is open or waiting to open, and for a stage that is gone: the recorder and session may be freed. */
    val recordClosed: Boolean get() = StageNative.stageRecordClosed(handle)

    fun exportSceneFactory(sceneId: String): SceneFactory {
        val id = sceneId
        return object : SceneFactory {
            override val sceneId: String = id
        }
    }

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
        StageNative.stageSetLook(
            handle,
            l.sceneId,
            l.params.frame(),
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
            StageNative.stageSetUnderlay(handle, u?.pixels, u?.width ?: 0, u?.height ?: 0, u?.blend ?: 0, u?.amount ?: 0f)
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
        ringPoll()
    }

    /** Native's event descriptor on the main looper: [takeEvents] runs when a frame left events, and not otherwise. */
    private fun listenForEvents() {
        val fd = StageNative.stageEventFd(handle)
        if (fd < 0) return
        val pfd = ParcelFileDescriptor.adoptFd(fd)
        eventFd = pfd
        main.looper.queue.addOnFileDescriptorEventListener(
            pfd.fileDescriptor,
            MessageQueue.OnFileDescriptorEventListener.EVENT_INPUT,
        ) { _, _ ->
            takeEvents()
            MessageQueue.OnFileDescriptorEventListener.EVENT_INPUT
        }
    }

    /** Hands native the ring the app hears now; goes on while the stage draws or just did. */
    private fun ringPoll() {
        val h = handle
        if (h == 0L) return
        posted = false
        val ring = soundInput()
        if (ring != heard) {
            StageNative.stageSetSound(h, ring)
            heard = ring
        }
        if (eventFd == null) takeEvents()
        val draws = StageNative.stageState(h) and StageNative.STAGE_STATE_VSYNC != 0
        if (draws || SystemClock.uptimeMillis() < pollUntilMs) {
            posted = true
            main.postDelayed(ringTick, RING_POLL_MS)
        }
    }

    private fun takeEvents() {
        val h = handle
        if (h == 0L) return
        val events = StageNative.stageTakeEvents(h)
        if (events and StageNative.FRAME_LIQUID_CHANGED != 0) liquid.value = StageNative.stageLiquidMode(h)
        if (events and StageNative.FRAME_ERROR_CHANGED != 0) onShaderError(StageNative.stageError(h)?.ifEmpty { null })
        if (events and StageNative.FRAME_DIRECTOR_MOMENT != 0 && StageNative.stageTakeDirectorMoments(h) > 0) onDirectorStyleMoment()
        if (events and StageNative.FRAME_STILL_READY != 0) deliverStill(h)
    }

    /** The still to whoever asked; one that cannot be read is asked for again. */
    private fun deliverStill(h: Long) {
        val then = stillThen ?: return
        val size = IntArray(2)
        val pixels = if (StageNative.stageStillSize(h, size)) ByteBuffer.allocateDirect(size[0] * size[1] * 4) else null
        if (pixels == null || !StageNative.stageStillRead(h, pixels)) {
            StageNative.stageStillRequest(h)
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
            if (h != 0L) StageNative.stageDestroy(h)
        }
    }

    private companion object {
        /** Long enough for a stopped stage's one frame (a redraw, a still) to finish and be read. */
        const val POLL_LINGER_MS = 5_000L

        /** How often the sound ring is looked at while the stage draws: a session that comes or goes is picked up within this. */
        const val RING_POLL_MS = 50L

        /** One daemon thread for every stage in the process. */
        val CLEANER: Cleaner = Cleaner.create()
    }
}
