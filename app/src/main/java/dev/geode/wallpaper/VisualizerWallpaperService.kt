package dev.geode.wallpaper

import android.content.Context
import android.opengl.GLSurfaceView
import android.service.wallpaper.WallpaperService
import android.view.MotionEvent
import android.view.SurfaceHolder
import dev.geode.audio.AudioBus
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PresetStore
import dev.geode.render.FramePacer
import dev.geode.render.FrameRatePolicy
import dev.geode.render.TouchField
import dev.geode.render.VisualizerRenderer
import dev.geode.ui.ThemeStore
import dev.geode.util.bestEffort

class VisualizerWallpaperService : WallpaperService() {
    override fun onCreateEngine(): Engine = VisualizerEngine()

    private inner class VisualizerEngine : Engine() {
        private var glView: WallpaperGlSurfaceView? = null
        private var renderer: VisualizerRenderer? = null
        private val idle = IdleFeatures()
        private var lastFrameMs = 0L
        private var feeder: Thread? = null

        /**
         * Each engine instance — preview and home screen can both be live at once — owns its
         * own pacer, so the two never share a cadence or a frame-time window.
         */
        private val pacer =
            FramePacer(FrameRatePolicy.Capped(WALLPAPER_FPS)) { glView?.requestRender() }

        @Volatile
        private var running = false

        /**
         * Distinguishes one feeder run from the next.
         *
         * A wallpaper toggles visibility every time the screen sleeps or the user switches app, so
         * over a day this runs hundreds of times. If a join ever times out, the old thread must be
         * able to tell that a newer run has taken over and exit — otherwise it keeps ticking, and
         * keeps the renderer and the service context alive, for the rest of the process.
         */
        @Volatile
        private var feedGeneration = 0

        private var surfaceAvailable = false
        private var visible = false

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
            engine.pcmProvider = { null }
            glView =
                WallpaperGlSurfaceView(this@VisualizerWallpaperService).apply {
                    setEGLContextClientVersion(3)
                    setRenderer(engine)
                    // The pacer asks for each frame; without a request the GL thread parks, which
                    // is how "invisible costs nothing" is enforced rather than merely intended.
                    renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                }
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
            prefs.getString("milk_path", null)?.let { path ->
                if (java.io.File(path).isFile) engine.loadMilkPreset(path)
            }
            engine.reducedMotion = ThemeStore(prefsFiles.general).loadGui().reducedMotion
        }

        private fun startFeeding(engine: VisualizerRenderer) {
            if (feeder != null) return
            AudioBus.addConsumer()
            val generation = ++feedGeneration
            running = true
            lastFrameMs = android.os.SystemClock.elapsedRealtime()
            feeder =
                Thread {
                    while (running && feedGeneration == generation) {
                        val now = android.os.SystemClock.elapsedRealtime()
                        val dt = ((now - lastFrameMs).coerceIn(1, 100)) / 1000f
                        lastFrameMs = now
                        engine.features = AudioBus.features() ?: idle.tick(dt)
                        Thread.sleep(FEED_INTERVAL_MS)
                    }
                }.apply {
                    isDaemon = true
                    name = "geode-wallpaper-audio"
                    start()
                }
        }

        private fun stopFeeding() {
            val thread = feeder ?: return
            running = false
            feedGeneration++
            AudioBus.removeConsumer()
            runCatching { thread.join(FEEDER_JOIN_MS) }
            feeder = null
        }

        private fun syncRunState() {
            val engine = renderer ?: return
            if (surfaceAvailable && visible) {
                glView?.onResume()
                pacer.start()
                startFeeding(engine)
            } else {
                // Stopped first, and on this same main-thread callback: the vsync callback is gone
                // before onPause is even asked for, so no frame can be requested after the
                // wallpaper stops being visible.
                pacer.stop()
                glView?.onPause()
                stopFeeding()
            }
        }

        override fun onVisibilityChanged(visible: Boolean) {
            super.onVisibilityChanged(visible)
            this.visible = visible
            // Going away with a finger still down is not guaranteed to come with a cancel,
            // and a pointer that is never retired stays pinned across the whole sleep.
            if (!visible) renderer?.submitTouchPoints(touchPoints, 0)
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
            // A rotation or a fold replaces the surface, and the rate preference lives on the
            // surface, so it has to be restated rather than assumed to have survived.
            pacer.applyTo(holder.surface)
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
            pacer.stop()
            stopFeeding()
            releaseScenesOnGlThread()
            glView?.destroy()
            glView = null
            renderer = null
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

        const val FEED_INTERVAL_MS = 16L

        const val FEEDER_JOIN_MS = 200L

        /**
         * The same target the app renders at, deliberately — not the 24–30 fps §4.4 of the
         * quality bar wants from a wallpaper.
         *
         * The reduced rate is the right end state, but it cannot be switched on from here alone:
         * `FluidSceneBase.autoQualityTick` reads GPU pressure off the achieved frame rate through
         * `PerformanceMonitor(targetFps = 50f)`, an inference that only holds while the renderer
         * free-runs. Ask for 30 and every fluid scene on the wallpaper reads its own cap as a
         * device that cannot keep up and downgrades itself two quality tiers within three
         * seconds. Teaching that monitor the difference between "capped" and "struggling" comes
         * first; then this becomes a one-line change.
         */
        const val WALLPAPER_FPS = FramePacer.DEFAULT_TARGET_FPS
    }
}
