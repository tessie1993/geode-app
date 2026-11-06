package dev.geode.wallpaper

import android.app.KeyguardManager
import android.app.WallpaperManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.service.wallpaper.WallpaperService
import android.view.MotionEvent
import android.view.SurfaceHolder
import androidx.core.content.ContextCompat
import dev.geode.billing.ProSnapshot
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceStore
import dev.geode.data.PresetStore
import dev.geode.data.ThemeStore
import dev.geode.pro.ProFeature
import dev.geode.render.FramePacer
import dev.geode.render.FrameRatePolicy
import dev.geode.render.HandMask
import dev.geode.render.TouchField
import dev.geode.render.VisualizerRenderer
import dev.geode.util.bestEffort

class VisualizerWallpaperService : WallpaperService() {
    override fun onCreateEngine(): Engine = VisualizerEngine()

    private inner class VisualizerEngine : Engine() {
        private var glView: WallpaperGlSurfaceView? = null
        private var renderer: VisualizerRenderer? = null
        private var wallpaperRenderer: WallpaperRenderer? = null

        /**
         * Each engine instance — preview and home screen can both be live at once — owns its
         * own pacer, so the two never share a cadence or a frame-time window.
         */
        private val pacer =
            FramePacer(FrameRatePolicy.Capped(WALLPAPER_FPS)) { glView?.requestRender() }

        private var surfaceAvailable = false
        private var visible = false

        /** The shade or recents is over the wallpaper; see [onZoomChanged]. */
        private var covered = false

        private val prefsStore = WallpaperPrefsStore(this@VisualizerWallpaperService)
        private var settings = WallpaperSettings()

        /**
         * What the wallpaper hears: the app's ring while MuzViz feeds it, else any app's audio.
         * Its silence flips restate the run mode, which is how "rest when silent" happens.
         */
        private val sound = SoundSource(this@VisualizerWallpaperService) { syncRunState() }

        /** Whether [sound] is running; it only restarts on a real change (see [syncListening]). */
        private var listening = false

        private var runMode = WallpaperRunMode.STOPPED
        private val handler = Handler(Looper.getMainLooper())

        /** Ends the settle burst of [WallpaperRunMode.STILL]. */
        private val endStillSettle = Runnable { if (runMode == WallpaperRunMode.STILL) pacer.stop() }

        private val powerManager = getSystemService(PowerManager::class.java)
        private val keyguardManager = getSystemService(KeyguardManager::class.java)

        /**
         * Battery saver switching, and the unlock that turns the lock screen's look into the home
         * screen's. Registered for the engine's whole life, not only while visible: both can change
         * while the wallpaper is hidden, and a hidden wallpaper does nothing on the broadcast but
         * restate a stopped state.
         */
        private val systemReceiver =
            object : BroadcastReceiver() {
                override fun onReceive(
                    context: Context,
                    intent: Intent,
                ) = syncRunState()
            }

        /**
         * Reused across touch events: a wallpaper is touched at the panel's rate, and this
         * runs on the main thread the launcher is also drawing on.
         */
        private val touchPoints = FloatArray(TouchField.MAX_POINTS * 2)

        /**
         * The surface, not the display: a wallpaper surface is often wider than the screen
         * so the launcher can pan it, and [MotionEvent] coordinates are in that same
         * surface space. Dividing by the display width would put every finger in the wrong
         * place on exactly the devices that scroll their home screens.
         */
        private var surfaceWidth = 1f
        private var surfaceHeight = 1f

        /** Where the screen sits on a panning surface, in surface pixels; see [onOffsetsChanged]. */
        private var panX = 0f
        private var panY = 0f

        private inner class WallpaperGlSurfaceView(
            context: Context,
        ) : GLSurfaceView(context) {
            override fun getHolder(): SurfaceHolder = surfaceHolder

            fun destroy() {
                super.onDetachedFromWindow()
            }
        }

        override fun onCreate(holder: SurfaceHolder) {
            super.onCreate(holder)
            // Off by default on a wallpaper engine, so onTouchEvent is never called without
            // this line — which is why the wallpaper had no touch at all.
            setTouchEventsEnabled(true)
            val engine = VisualizerRenderer(this@VisualizerWallpaperService)
            renderer = engine
            restoreLiveState(engine)
            settings = prefsStore.load()
            engine.pcmProvider = { null }
            engine.soundInput = { sound.input }
            val wrapped = WallpaperRenderer(engine)
            wallpaperRenderer = wrapped
            glView =
                WallpaperGlSurfaceView(this@VisualizerWallpaperService).apply {
                    setEGLContextClientVersion(3)
                    // Before setRenderer, like VisualizerView: syncRunState pauses this view on every
                    // trip off the home screen (an app opening, the screen locking), and without it
                    // each return rebuilt the EGL context and with it every scene, shader and FBO,
                    // a black flash and a stall on exactly the transition people watch. The driver
                    // may still refuse to keep it; VisualizerRenderer.onSurfaceCreated is safe to run
                    // again on a kept native renderer, so the lost-context path stays correct.
                    preserveEGLContextOnPause = true
                    setRenderer(wrapped)
                    // The pacer asks for each frame; without a request the GL thread parks, which
                    // is how "invisible costs nothing" is enforced rather than merely intended.
                    renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                }
            ContextCompat.registerReceiver(
                this@VisualizerWallpaperService,
                systemReceiver,
                IntentFilter().apply {
                    addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED)
                    addAction(Intent.ACTION_USER_PRESENT)
                },
                ContextCompat.RECEIVER_NOT_EXPORTED,
            )
        }

        private fun restoreLiveState(engine: VisualizerRenderer) {
            val prefsFiles = GeodePrefsFiles(this@VisualizerWallpaperService)
            val prefs = prefsFiles.viz
            prefs.getString("live_state", null)?.let { json ->
                runCatching { PresetStore.fromJson(json) }.getOrNull()?.let { preset ->
                    engine.requestedSceneId = preset.sceneId
                    engine.sceneParams = preset.params
                }
            }
            // The MilkDrop preset needs nothing here: the renderer restores the app's (MilkPresetMemory).
            val gui = ThemeStore(prefsFiles.general).loadGui()
            engine.reducedMotion = gui.reducedMotion
            engine.soundTuning = gui.soundTuning
            // Settings › Performance; the wallpaper keeps its own pacer, so the cap goes there.
            val performance = PerformanceStore(prefsFiles.general, this@VisualizerWallpaperService).settings.value
            engine.performance = performance
            pacer.policy = FrameRatePolicy.Capped(minOf(WALLPAPER_FPS, performance.fpsCap.toFloat()))
            // The user's modulation too: without it the wallpaper and the dream drew the look
            // with every LFO, envelope and custom reaction switched off.
            val modulation = dev.geode.data.LfoStore(prefsFiles.general)
            engine.lfoEngine.configs = modulation.load()
            engine.adsrEngine.configs = modulation.loadAdsrs()
            engine.reactions = modulation.loadReactions()
            HandMask.set(modulation.loadHandMask())
        }

        /**
         * Restates the run mode from scratch ([wallpaperRunMode] has the rule). Called on every
         * change that can move it: visibility, surface, cover, battery saver, unlock, silence.
         */
        private fun syncRunState() {
            if (renderer == null) return
            // Pro feature. Re-read on every change, so a purchase or a trial made in the app
            // animates the wallpaper the next time the home screen shows.
            val animated = ProSnapshot.gate(this@VisualizerWallpaperService).allows(ProFeature.LIVE_WALLPAPER)
            val conditions =
                WallpaperConditions(
                    surfaceAvailable = surfaceAvailable,
                    visible = visible,
                    animationAllowed = animated,
                    preview = isPreview,
                    covered = covered,
                    batterySaver = powerManager?.isPowerSaveMode == true,
                    silent = false,
                    settings = settings,
                )
            // Listen whenever the wallpaper would animate if there were sound: that is what wakes
            // a resting wallpaper. Hidden, battery saver and free hear nothing. A cover (the shade,
            // recents) keeps listening: pulling the shade is a second's glance, and stopping would
            // release the capture and restart the silence clock each time.
            syncListening(wallpaperRunMode(conditions.copy(covered = false)) == WallpaperRunMode.ANIMATED)
            wallpaperRenderer?.edge = currentLook() == WallpaperLook.EDGE
            val previous = runMode
            runMode = wallpaperRunMode(conditions.copy(silent = sound.silent))
            if (runMode != WallpaperRunMode.STILL) handler.removeCallbacks(endStillSettle)
            when (runMode) {
                WallpaperRunMode.ANIMATED -> {
                    glView?.onResume()
                    pacer.start()
                }
                WallpaperRunMode.STILL -> {
                    // A still frame of the style and then no pacing, so it costs nothing while it
                    // sits there: without Pro, in battery saver, or resting in silence. Paced for
                    // [STILL_SETTLE_MS] first, because a new scene is built across frames
                    // (ScenePrep): one frame on a fresh engine would hold a black or half-built
                    // picture. Already still, a restated look needs only one more frame.
                    glView?.onResume()
                    if (previous != WallpaperRunMode.STILL) {
                        pacer.start()
                        handler.postDelayed(endStillSettle, STILL_SETTLE_MS)
                    } else {
                        glView?.requestRender()
                    }
                }
                WallpaperRunMode.STOPPED -> {
                    // Stopped first, and on this same main-thread callback: the vsync callback is gone
                    // before onPause is even asked for, so no frame can be requested after the
                    // wallpaper stops being visible.
                    pacer.stop()
                    glView?.onPause()
                }
            }
        }

        /**
         * Starts or stops [sound] only on a real change: a start counts as fresh sound, so
         * restarting on every sync would reset the silence clock and the wallpaper would never rest.
         */
        private fun syncListening(listen: Boolean) {
            if (listen == listening) return
            listening = listen
            if (listen) sound.start(settings.hearOtherApps) else sound.stop()
        }

        /**
         * The look for the screen this engine is drawing right now. Android 14 says which screens
         * an engine draws ([getWallpaperFlags]): a lock-only engine is always the lock screen and a
         * home-only one never is. One engine on both, and every older release, asks the keyguard.
         */
        private fun currentLook(): WallpaperLook {
            if (isPreview) return settings.homeLook
            val keyguardShowing = keyguardManager?.isKeyguardLocked == true
            val onLock =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                    when (wallpaperFlags) {
                        WallpaperManager.FLAG_LOCK -> true
                        WallpaperManager.FLAG_SYSTEM -> false
                        else -> keyguardShowing
                    }
                } else {
                    keyguardShowing
                }
            return if (onLock) settings.lockLook else settings.homeLook
        }

        override fun onVisibilityChanged(visible: Boolean) {
            super.onVisibilityChanged(visible)
            this.visible = visible
            // Going away with a finger still down is not guaranteed to come with a cancel,
            // and a pointer that is never retired stays pinned across the whole sleep.
            if (!visible) renderer?.submitTouchPoints(touchPoints, 0)
            // Re-read each time it shows: the options page, opened from the picker, covers it.
            if (visible) settings = prefsStore.load()
            syncRunState()
        }

        /**
         * Android 11+ zooms the wallpaper out behind the notification shade, recents and some
         * launchers' app drawers, which leave it "visible" under a blur. Past [COVER_ZOOM] it
         * stops: nobody watches a visual through a frosted shade.
         */
        override fun onZoomChanged(zoom: Float) {
            super.onZoomChanged(zoom)
            val nowCovered = zoom >= COVER_ZOOM
            if (nowCovered == covered) return
            covered = nowCovered
            syncRunState()
        }

        override fun onWallpaperFlagsChanged(which: Int) {
            super.onWallpaperFlagsChanged(which)
            syncRunState()
        }

        override fun onSurfaceCreated(holder: SurfaceHolder) {
            super.onSurfaceCreated(holder)
            surfaceAvailable = true
            pacer.applyTo(holder.surface)
            syncRunState()
        }

        override fun onSurfaceChanged(
            holder: SurfaceHolder,
            format: Int,
            width: Int,
            height: Int,
        ) {
            super.onSurfaceChanged(holder, format, width, height)
            surfaceWidth = width.toFloat().coerceAtLeast(1f)
            surfaceHeight = height.toFloat().coerceAtLeast(1f)
            updateVisibleWindow()
            // A rotation or a fold replaces the surface, and the rate preference lives on the
            // surface, so it has to be restated rather than assumed to have survived.
            pacer.applyTo(holder.surface)
        }

        /**
         * The launcher's pan, so the edge look draws on the screen's edge rather than the
         * surface's. [xPixels] and [yPixels] are the surface's offset from the screen (≤ 0).
         */
        override fun onOffsetsChanged(
            xOffset: Float,
            yOffset: Float,
            xOffsetStep: Float,
            yOffsetStep: Float,
            xPixels: Int,
            yPixels: Int,
        ) {
            super.onOffsetsChanged(xOffset, yOffset, xOffsetStep, yOffsetStep, xPixels, yPixels)
            panX = -xPixels.toFloat()
            panY = -yPixels.toFloat()
            updateVisibleWindow()
        }

        private fun updateVisibleWindow() {
            val metrics = resources.displayMetrics
            // Within a nav bar's worth of the display, the surface is the screen.
            val wide = surfaceWidth > metrics.widthPixels * PAN_SLACK
            val tall = surfaceHeight > metrics.heightPixels * PAN_SLACK
            wallpaperRenderer?.setVisibleWindow(
                if (wide) panX else 0f,
                if (tall) panY else 0f,
                if (wide) metrics.widthPixels.toFloat() else surfaceWidth,
                if (tall) metrics.heightPixels.toFloat() else surfaceHeight,
            )
        }

        /**
         * The wallpaper's whole input: publish the live pointers, in the same y-up NDC the
         * app publishes, so a scene behaves identically in both hosts.
         *
         * There is no smear or pinch here on purpose. Those write to the visual params, and
         * a wallpaper has no controls to undo them with — a stray pinch on the home screen
         * would leave the wallpaper zoomed with no way back. Publishing pointers is
         * transient by construction: it decays to nothing the moment the finger leaves.
         */
        override fun onTouchEvent(event: MotionEvent) {
            super.onTouchEvent(event)
            val engine = renderer ?: return
            val action = event.actionMasked
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                engine.submitTouchPoints(touchPoints, 0)
                return
            }
            // ACTION_POINTER_UP names a finger that is leaving, and that finger is still in
            // the event's pointer list. Publishing it would keep steering the visuals with a
            // finger the user has already lifted.
            val leaving = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            var live = 0
            for (i in 0 until event.pointerCount) {
                if (i != leaving && live < TouchField.MAX_POINTS) {
                    touchPoints[live * 2] = event.getX(i) / surfaceWidth * 2f - 1f
                    touchPoints[live * 2 + 1] = 1f - event.getY(i) / surfaceHeight * 2f
                    live++
                }
            }
            engine.submitTouchPoints(touchPoints, live)
        }

        override fun onSurfaceDestroyed(holder: SurfaceHolder) {
            surfaceAvailable = false
            syncRunState()
            super.onSurfaceDestroyed(holder)
        }

        override fun onDestroy() {
            handler.removeCallbacks(endStillSettle)
            pacer.stop()
            sound.stop()
            bestEffort(TAG, "unregister system receiver") { unregisterReceiver(systemReceiver) }
            releaseScenesOnGlThread()
            glView?.destroy()
            glView = null
            renderer = null
            wallpaperRenderer = null
            super.onDestroy()
        }

        /**
         * The engine is discarded here, and MilkdropScene's projectM instance lives in the native
         * heap, where losing the GL context reclaims nothing. A wallpaper engine is created and
         * destroyed every time the screen sleeps or the user switches launcher screens, so without
         * this each of those leaks one engine into a process that keeps running.
         *
         * Queued before destroy() because GLSurfaceView's thread stops draining events once it is
         * asked to exit, and waited on - briefly - because that thread is gone afterwards.
         */
        private fun releaseScenesOnGlThread() {
            val view = glView ?: return
            val active = renderer ?: return
            val done = java.util.concurrent.CountDownLatch(1)
            view.queueEvent {
                bestEffort(TAG, "active.releaseScenes()") { active.releaseScenes() }
                done.countDown()
            }
            bestEffort(TAG, "await scene release") {
                done.await(GL_RELEASE_WAIT_MS, java.util.concurrent.TimeUnit.MILLISECONDS)
            }
        }
    }

    private companion object {
        const val TAG = "VisualizerWallpaper"

        /** Bounded on purpose: a wedged GL thread must not turn teardown into an ANR. */
        const val GL_RELEASE_WAIT_MS = 200L

        /** Wallpaper zoom (0 none, 1 fully zoomed out) past which the shade or recents covers it. */
        const val COVER_ZOOM = 0.9f

        /** A surface this much bigger than the display is a panning one. */
        const val PAN_SLACK = 1.1f

        /** How long a still frame keeps drawing first, so a scene being built finishes on screen. */
        const val STILL_SETTLE_MS = 2_000L

        /**
         * The same target the app renders at, deliberately — not the 24–30 fps §4.4 of the
         * quality bar wants from a wallpaper.
         *
         * The reduced rate is the right end state, but it cannot be switched on from here alone:
         * the native fluid quality ladder (`FluidSceneBase::autoQualityTick` in `core/viz/scenes`)
         * reads GPU pressure off the achieved frame rate through its `PerformanceMonitor`
         * (target 50 fps), an inference that only holds while the renderer
         * free-runs. Ask for 30 and every fluid scene on the wallpaper reads its own cap as a
         * device that cannot keep up and downgrades itself two quality tiers within three
         * seconds. Teaching that monitor the difference between "capped" and "struggling" comes
         * first; then this becomes a one-line change. The same is why silence and battery saver
         * hold a still frame rather than dropping to a low rate.
         */
        const val WALLPAPER_FPS = FramePacer.DEFAULT_TARGET_FPS
    }
}
