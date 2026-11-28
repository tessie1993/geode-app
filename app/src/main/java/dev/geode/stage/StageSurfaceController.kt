package dev.geode.stage

import android.app.Presentation
import android.content.res.Configuration
import android.graphics.Rect
import android.hardware.DisplayManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Process
import android.view.Display
import android.view.FrameLayout
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.view.WindowManager
import dev.geode.data.DisplayPanel
import dev.geode.data.PerformanceSettings
import dev.geode.engine.bridge.StageNative
import dev.geode.geodeContainer
import java.lang.ref.WeakReference

/**
 * The non-View owner of GameActivity's [SurfaceView]. It tells native what Android tells it (the surface, its
 * size, whether the view is attached, visible and opaque, the host's pause) and decides nothing: when the stage
 * draws, sheds or listens to the tilt sensor is native's answer.
 *
 * The stage outlives the surface, a detach (a second screen takes the view and gives it back) and the Activity's
 * view itself (an Activity recreation builds a new controller over the same stage), so whoever made the stage
 * ends it with `stage.close()`. [release] only detaches this controller.
 */
class StageSurfaceController(
    val surfaceView: SurfaceView,
    val activityRoot: FrameLayout,
) : SurfaceHolder.Callback2,
    SensorEventListener {
    private val context = surfaceView.context
    private val holder = surfaceView.holder
    private val sensors = context.getSystemService(SensorManager::class.java)
    private val displays = context.getSystemService(DisplayManager::class.java)
    private val mainHandler = Handler(Looper.getMainLooper())

    // The game rotation vector, not the plain one: no magnetometer, so a speaker cannot swing the view.
    private val tilt = sensors?.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)

    @Volatile
    private var released = false

    @Volatile
    private var attachedStage: Stage? = null

    /** Invalidates observer work from an attachment that has been detached or released. */
    private var ownerGeneration = 0L

    /** A surface must be created before its change/redraw callbacks are current. */
    private var surfaceGeneration = 0L
    private var activeHolder: SurfaceHolder? = null
    private var pendingSurface: Surface? = null
    private var pendingFormat = 0
    private var pendingWidth = 0
    private var pendingHeight = 0
    private var pendingRedraw = false

    /** Null until the host has actually stated pause or resume; native's current state is left alone before then. */
    private var paused: Boolean? = null
    private var pendingPerformance: PerformanceSettings? = null

    // The window buffer's size as last fixed (0: not yet), and the blur region as last set, in this view's terms.
    private var bufferWidth = 0
    private var bufferHeight = 0
    private val blur = FloatArray(BLUR_FIELDS)

    // Activity layout is remembered while the same view is owned by a Presentation.
    private var hostRect: Rect? = null
    private var hostAlpha = 1f
    private var presentation: Presentation? = null

    private var observedTree: ViewTreeObserver? = null
    private var observedDisplayKey: DisplayKey? = null

    @Volatile
    private var tiltOn = false
    private var tiltThread: HandlerThread? = null

    @Volatile
    private var tiltDisplay: Display? = null

    @Volatile
    private var tiltGeneration = 0L

    /** The attached Stage. Compose reads this only after [AppContent] has synchronously attached the retained Stage. */
    val stage: Stage
        get() = checkNotNull(currentStage()) { "StageSurfaceController has no attached Stage" }

    /** The host's keep-screen-on choice, applied to whichever window currently owns [surfaceView]. */
    var keepScreenOn: Boolean
        get() = surfaceView.keepScreenOn
        set(on) {
            surfaceView.keepScreenOn = on
            syncPresentationKeepScreenOn()
        }

    /** The camera's "Tilt phone to look" setting; native says when the sensor runs. */
    @Volatile
    var gyroWanted: Boolean = false
        set(on) {
            field = on
            if (!released) syncTilt()
        }

    /** The display currently showing [surfaceView], including a Presentation's display. */
    val display: Display?
        get() = surfaceView.display

    /** The surface view's current layout width in pixels. */
    val width: Int
        get() = surfaceView.width

    /** The surface view's current layout height in pixels. */
    val height: Int
        get() = surfaceView.height

    private val attachStateListener =
        object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(v: View) {
                if (released) return
                updateObservers()
                syncDisplay()
                restate(attached = true)
            }

            override fun onViewDetachedFromWindow(v: View) {
                // isAttachedToWindow can still be true here, so the detach is stated outright.
                removeObservers()
                if (!released) restate(attached = false)
            }
        }

    private val globalLayoutListener =
        ViewTreeObserver.OnGlobalLayoutListener {
            val generation = ownerGeneration
            if (released || generation != ownerGeneration) return@OnGlobalLayoutListener
            syncDisplay()
            restate()
        }

    private val windowVisibilityListener =
        ViewTreeObserver.OnWindowVisibilityChangeListener { visibility ->
            val generation = ownerGeneration
            if (released || generation != ownerGeneration) return@OnWindowVisibilityChangeListener
            restate(attached = surfaceView.isAttachedToWindow)
        }

    private val displayListener =
        object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(displayId: Int) = Unit

            override fun onDisplayRemoved(displayId: Int) {
                if (!released && displayId == observedDisplayKey?.displayId) {
                    syncDisplay()
                    restate()
                }
            }

            override fun onDisplayChanged(displayId: Int) {
                if (!released && displayId == (display?.displayId ?: observedDisplayKey?.displayId)) {
                    syncDisplay()
                    restate()
                }
            }
        }

    init {
        // GameActivity already owns a callback on this holder; this is only an additional one.
        holder.addCallback(this)
        surfaceView.addOnAttachStateChangeListener(attachStateListener)
        displays?.registerDisplayListener(displayListener, mainHandler)
        captureCurrentHolder()
        updateObservers()
        syncDisplay()
    }

    /** Claims this Stage and replays the surface and settings that arrived before the claim. */
    fun attach(stage: Stage) {
        if (released) return
        if (attachedStage === stage && stage.view.get() === this) {
            restate()
            syncTilt()
            return
        }
        attachedStage?.let(::detach)
        attachedStage = stage
        ownerGeneration++
        stage.view = WeakReference(this)
        replayPending(stage)
    }

    /** Detaches only [stage], and only while this controller still owns it. The Stage remains alive. */
    fun detach(stage: Stage) {
        if (released || attachedStage !== stage) return
        val owned = stage.view.get() === this
        attachedStage = null
        ownerGeneration++
        stopTilt()
        if (owned) {
            stage.setWindow(null, 0, 0)
            stage.setView(0)
            stage.view = WeakReference(null)
        }
    }

    /** Detaches and unregisters everything. It deliberately never calls [Stage.close]. */
    fun release() {
        if (released) return
        released = true
        ownerGeneration++
        surfaceGeneration++
        val stage = attachedStage
        val owned = stage != null && stage.view.get() === this
        attachedStage = null
        stopTilt()
        if (owned && stage != null) {
            stage.setWindow(null, 0, 0)
            stage.setView(0)
            stage.view = WeakReference(null)
        }
        holder.removeCallback(this)
        surfaceView.removeOnAttachStateChangeListener(attachStateListener)
        removeObservers()
        displays?.unregisterDisplayListener(displayListener)
        presentation = null
        activeHolder = null
        pendingSurface = null
        pendingFormat = 0
        pendingWidth = 0
        pendingHeight = 0
        pendingRedraw = false
    }

    /**
     * Settings › Performance for this surface: quality and the render resolution (the window buffer's size).
     * Live frame rate is fixed at 30 FPS; display refresh rates are not a Compose-side vote. Before [attach],
     * the values wait for replay.
     */
    fun applyPerformance(settings: PerformanceSettings) {
        if (released) return
        val fixed = settings.withFixedLiveFps()
        pendingPerformance = fixed
        currentStage()?.let { stage ->
            stage.performance = fixed
            stage.setLiveFps(PerformanceSettings.LIVE_FPS, floatArrayOf())
        }
        sizeBuffer()
    }

    /**
     * Places the Activity-owned surface inside [activityRoot]. Coordinates are Activity-root pixels. While a
     * Presentation owns the view, the values are remembered but do not alter the Presentation surface.
     */
    fun layoutHost(
        rect: Rect,
        alpha: Float,
    ) {
        if (released) return
        hostRect = Rect(rect)
        hostAlpha = alpha.coerceIn(0f, 1f)
        if (presentation != null) return
        applyHostLayout()
        restate()
    }

    /** Moves this same surface into [presentation]; the caller still owns show, dismiss and failure recovery. */
    fun moveToPresentation(presentation: Presentation) {
        if (released) return
        this.presentation = presentation
        (surfaceView.parent as? ViewGroup)?.removeView(surfaceView)
        surfaceView.alpha = 1f
        try {
            presentation.setContentView(
                surfaceView,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                ),
            )
        } catch (error: Exception) {
            this.presentation = null
            restoreToActivity()
            throw error
        }
        syncPresentationKeepScreenOn()
        if (!released) {
            syncDisplay()
            restate()
        }
    }

    /** Re-inserts the surface at index 0, behind the Compose sibling, and restores its remembered host layout. */
    fun restoreToActivity() {
        presentation = null
        (surfaceView.parent as? ViewGroup)?.removeView(surfaceView)
        activityRoot.addView(surfaceView, 0, hostLayoutParams())
        surfaceView.alpha = hostAlpha
        if (!released) {
            updateObservers()
            syncDisplay()
            restate()
        }
    }

    /** A display, density, orientation or window-size change can alter both the panel and the render buffer. */
    fun onConfigurationChanged(config: Configuration) {
        if (released) return
        syncDisplay()
        restate()
    }

    /**
     * Frosted glass for a Compose sheet floating over this visual. The sheet lives in another layer, so a
     * `RenderEffect` blur there never sees the visual's pixels; the visual blurs the region under the sheet
     * itself (`core/viz/UiBlurPass`) and the sheet draws its translucent glass over that.
     *
     * [left], [top], [right], [bottom] are the sheet's bounds over this view's width and height, 0..1 with y
     * down. [strength] 0..1 mixes the blur in (0 = off, no GPU work). [cornerPx] is the sheet's corner radius
     * in this view's pixels. Call when one of them changes; the stage keeps the region.
     */
    fun setUiBlurRegion(
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        strength: Float,
        cornerPx: Float,
    ) {
        if (released) return
        blur[0] = left
        blur[1] = top
        blur[2] = right
        blur[3] = bottom
        blur[4] = strength
        blur[5] = cornerPx
        sendBlur()
    }

    /** The host's pause and resume (the Activity's). */
    fun onPause() {
        if (released) return
        paused = true
        currentStage()?.pause()
        syncTilt()
    }

    /** The host's pause and resume (the Activity's). */
    fun onResume() {
        if (released) return
        paused = false
        currentStage()?.resume()
        syncTilt()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (released || holder !== this.holder) return
        surfaceGeneration++
        activeHolder = holder
        val frame = holder.surfaceFrame
        pendingSurface = holder.surface
        pendingFormat = 0
        pendingWidth = frame.width()
        pendingHeight = frame.height()
        currentStage()?.setWindow(pendingSurface, pendingWidth, pendingHeight)
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int,
    ) {
        if (released || holder !== this.holder || surfaceGeneration == 0L) return
        if (activeHolder !== holder) captureCurrentHolder()
        if (activeHolder !== holder) return
        pendingSurface = holder.surface
        pendingFormat = format
        pendingWidth = width
        pendingHeight = height
        currentStage()?.setSize(width, height)
    }

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) {
        if (released || holder !== this.holder || activeHolder !== holder || surfaceGeneration == 0L) return
        pendingRedraw = true
        currentStage()?.let { stage ->
            stage.redraw()
            pendingRedraw = false
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (released || holder !== this.holder || activeHolder !== holder) return
        val destroyedGeneration = surfaceGeneration
        if (destroyedGeneration == 0L) return
        surfaceGeneration = destroyedGeneration + 1L
        activeHolder = null
        pendingSurface = null
        pendingFormat = 0
        pendingWidth = 0
        pendingHeight = 0
        pendingRedraw = false
        currentStage()?.setWindow(null, 0, 0)
    }

    override fun onSensorChanged(event: SensorEvent) {
        val generation = tiltGeneration
        if (!tiltOn || generation == 0L) return // one queued as the sensor stopped would start a new session
        val stage = currentStage() ?: return
        val values = event.values
        if (values.size < 4 || generation != tiltGeneration || !tiltOn) return
        stage.gyroPush(values[0], values[1], values[2], values[3], tiltDisplay?.rotation ?: 0)
    }

    override fun onAccuracyChanged(
        sensor: Sensor?,
        accuracy: Int,
    ) = Unit

    private fun currentStage(): Stage? {
        val stage = attachedStage ?: return null
        return if (!released && stage.view.get() === this) stage else null
    }

    private fun replayPending(stage: Stage) {
        val surface = pendingSurface
        if (surface != null && surface.isValid && activeHolder === holder) {
            stage.setWindow(surface, pendingWidth, pendingHeight)
        }
        if (pendingWidth > 0 && pendingHeight > 0) stage.setSize(pendingWidth, pendingHeight)
        if (pendingRedraw) {
            stage.redraw()
            pendingRedraw = false
        }
        pendingPerformance?.let { settings ->
            stage.performance = settings
            stage.setLiveFps(PerformanceSettings.LIVE_FPS, floatArrayOf())
        }
        // A retained Stage may still carry the previous controller's blur when none has been set yet.
        sendBlur()
        when (paused) {
            true -> stage.pause()
            false -> stage.resume()
            null -> Unit
        }
        tiltDisplay = display
        sizeBuffer()
        restate()
        syncTilt()
    }

    private fun captureCurrentHolder() {
        if (released || activeHolder === holder) return
        val surface = holder.surface ?: return
        if (!surface.isValid) return
        surfaceGeneration++
        activeHolder = holder
        val frame = holder.surfaceFrame
        pendingSurface = surface
        pendingFormat = 0
        pendingWidth = frame.width()
        pendingHeight = frame.height()
    }

    /** What the view sees of itself, as is. */
    private fun restate(attached: Boolean = surfaceView.isAttachedToWindow) {
        val flags =
            (if (attached) StageNative.STAGE_VIEW_ATTACHED else 0) or
                (if (surfaceView.windowVisibility == View.VISIBLE) StageNative.STAGE_VIEW_WINDOW_VISIBLE else 0) or
                (if (surfaceView.isShown) StageNative.STAGE_VIEW_SHOWN else 0) or
                (if (surfaceView.alpha > 0f) StageNative.STAGE_VIEW_OPAQUE else 0)
        currentStage()?.setView(flags)
        syncTilt()
    }

    private fun updateObservers() {
        val next = if (surfaceView.isAttachedToWindow) surfaceView.viewTreeObserver else null
        if (observedTree === next) return
        removeObservers()
        observedTree = next
        next?.addOnGlobalLayoutListener(globalLayoutListener)
        next?.addOnWindowVisibilityChangeListener(windowVisibilityListener)
    }

    private fun removeObservers() {
        val tree = observedTree ?: return
        if (tree.isAlive) {
            tree.removeOnGlobalLayoutListener(globalLayoutListener)
            tree.removeOnWindowVisibilityChangeListener(windowVisibilityListener)
        }
        observedTree = null
    }

    private fun syncDisplay() {
        if (released) return
        syncDisplayKey()
        tiltDisplay = display
        if (display != null) sizeBuffer()
    }

    /**
     * The render resolution the Android way: tells native the display's panel and modes and this view's size, then
     * fixes the window buffer to the size native published. Full size follows the layout again. Restated on every
     * layout size (rotation, a multi-window resize, a fold, a Presentation move) and setting; the holder keeps it
     * across a surface's recreation. Main thread, as `setFixedSize` asks.
     */
    private fun sizeBuffer() {
        val performance = context.geodeContainer.performance
        performance.view(DisplayPanel.of(display), width, height)
        val published = performance.resolution.value
        if (published.bufferWidth <= 0) return
        if (published.bufferWidth == bufferWidth && published.bufferHeight == bufferHeight) return
        bufferWidth = published.bufferWidth
        bufferHeight = published.bufferHeight
        if (bufferWidth == width && bufferHeight == height) holder.setSizeFromLayout() else holder.setFixedSize(bufferWidth, bufferHeight)
        if (blur[4] > 0f) sendBlur()
    }

    /** The edges are fractions of the view, so they hold; the corner is in pixels, so it is the buffer's. */
    private fun sendBlur() {
        val stage = currentStage() ?: return
        val toBuffer = if (width > 0 && bufferWidth > 0) bufferWidth.toFloat() / width else 1f
        stage.setUiBlurRegion(blur[0], blur[1], blur[2], blur[3], blur[4], blur[5] * toBuffer)
    }

    private fun syncDisplayKey() {
        val currentDisplay = display
        if (currentDisplay == null) {
            observedDisplayKey = null
            return
        }
        val currentMode = currentDisplay.mode
        observedDisplayKey =
            DisplayKey(
                displayId = currentDisplay.displayId,
                modeId = currentMode.modeId,
                physicalWidth = currentMode.physicalWidth,
                physicalHeight = currentMode.physicalHeight,
                refreshRate = currentMode.refreshRate,
            )
    }

    private fun hostLayoutParams(): FrameLayout.LayoutParams {
        val rect = hostRect
        return FrameLayout.LayoutParams(
            rect?.left ?: 0,
            rect?.top ?: 0,
            rect?.width()?.coerceAtLeast(0) ?: ViewGroup.LayoutParams.MATCH_PARENT,
            rect?.height()?.coerceAtLeast(0) ?: ViewGroup.LayoutParams.MATCH_PARENT,
        )
    }

    private fun applyHostLayout() {
        if (surfaceView.parent !== activityRoot) return
        surfaceView.layoutParams = hostLayoutParams()
        surfaceView.alpha = hostAlpha
    }

    private fun syncPresentationKeepScreenOn() {
        val window = presentation?.window ?: return
        if (surfaceView.keepScreenOn) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun syncTilt() {
        val sensor = tilt ?: return
        val stage = currentStage()
        val on = stage != null && stage.gyroListens(gyroWanted)
        tiltDisplay = display
        if (on == tiltOn) return
        // Only a registration the sensor service took counts as on, so a refused one is tried again next sync.
        if (on) {
            val thread = HandlerThread("GeodeTilt", Process.THREAD_PRIORITY_DEFAULT).apply { start() }
            tiltGeneration++
            tiltThread = null
            tiltOn = sensors?.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME, Handler(thread.looper)) == true
            if (tiltOn) {
                tiltThread = thread
            } else {
                thread.quitSafely()
            }
        } else {
            stopTilt()
        }
    }

    private fun stopTilt() {
        tiltOn = false
        tiltGeneration++
        sensors?.unregisterListener(this)
        tiltThread?.quitSafely()
        tiltThread = null
        tiltDisplay = null
    }

    private data class DisplayKey(
        val displayId: Int,
        val modeId: Int,
        val physicalWidth: Int,
        val physicalHeight: Int,
        val refreshRate: Float,
    )

    private companion object {
        /** [setUiBlurRegion]'s left, top, right, bottom, strength and corner. */
        const val BLUR_FIELDS = 6
    }
}
