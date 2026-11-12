package dev.geode.wallpaper

import android.app.KeyguardManager
import android.app.WallpaperManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.service.wallpaper.WallpaperService
import android.view.MotionEvent
import android.view.SurfaceHolder
import androidx.core.content.ContextCompat
import dev.geode.billing.ProSnapshot
import dev.geode.billing.WallClock
import dev.geode.data.LookStore
import dev.geode.data.PerformanceStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.pro.ProFeature
import dev.geode.render.TouchField
import dev.geode.stage.Stage
import dev.geode.util.bestEffort

class VisualizerWallpaperService : WallpaperService() {
    override fun onCreateEngine(): Engine = VisualizerEngine()

    private inner class VisualizerEngine : Engine() {
        /**
         * Each engine instance — preview and home screen can both be live at once — owns its
         * own stage, so the two never share a cadence or a frame-time window. Its cap is the
         * wallpaper's own fps choice ([WallpaperSettings.fps], see [applyFps]).
         */
        private var stage: Stage? = null

        /** The edge look's glow as last handed to the stage (`core/viz/frame/EdgeGlowPass`, #439). */
        private var glow = EdgeGlow()

        private var surfaceAvailable = false
        private var visible = false

        /** The shade or recents is over the wallpaper; see [onZoomChanged]. */
        private var covered = false

        private val prefsFiles = this@VisualizerWallpaperService.geodeContainer.prefsFiles
        private val prefsStore = WallpaperPrefsStore(prefsFiles.wallpaper)
        private val generalPrefs = prefsFiles.general

        /**
         * Settings › Performance, followed live: the app and the wallpaper share this process and
         * the prefs file, so a change reaches the running engine at once (split screen, or the app
         * in PiP over the home screen), not at its next start. A field: listeners are held weakly.
         */
        private val performanceChanged =
            SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key in PerformanceStore.KEYS) stage?.let(::applyPerformance)
            }
        private var settings = WallpaperSettings()

        /** The system's Remove animations; a flip restates the run mode at once. */
        private val systemAnimations = SystemAnimations(this@VisualizerWallpaperService) { syncRunState() }

        /**
         * What the wallpaper hears: the app's ring while BeatRealms feeds it, else any app's audio.
         * Its silence flips restate the run mode, which is how "rest when silent" happens.
         */
        private val sound = SoundSource(this@VisualizerWallpaperService) { syncRunState() }

        /** Whether [sound] is running; it only restarts on a real change (see [syncListening]). */
        private var listening = false

        private val handler = Handler(Looper.getMainLooper())

        /** Re-reads the gate when a running trial ends, so the wallpaper stills on time (#376). */
        private val trialEnded = Runnable { syncRunState() }

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

        /** The launcher's last [onOffsetsChanged], raw (`GEODE_WALLPAPER_OFFSETS`), and the window native makes of it. */
        private val offsets = FloatArray(6)
        private val window = FloatArray(4)

        override fun onCreate(holder: SurfaceHolder) {
            super.onCreate(holder)
            // Off by default on a wallpaper engine, so onTouchEvent is never called without
            // this line — which is why the wallpaper had no touch at all.
            setTouchEventsEnabled(true)
            val engine =
                Stage(this@VisualizerWallpaperService, GeodeNative.STAGE_KIND_SURFACE, GeodeNative.SURFACE_DEFAULT_FPS.toFloat())
            stage = engine
            restoreLook(engine)
            settings = prefsStore.load()
            applyFps()
            engine.soundInput = { sound.input }
            ContextCompat.registerReceiver(
                this@VisualizerWallpaperService,
                systemReceiver,
                IntentFilter().apply {
                    addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED)
                    addAction(Intent.ACTION_USER_PRESENT)
                },
                ContextCompat.RECEIVER_NOT_EXPORTED,
            )
            // For the engine's whole life, like the receiver: the setting can flip while hidden.
            systemAnimations.start()
            generalPrefs.registerOnSharedPreferenceChangeListener(performanceChanged)
        }

        /**
         * Caps the stage to the wallpaper's own fps choice, snapped to a listed rate natively.
         * Settings › Performance's cap is the live visual's, not this.
         */
        private fun applyFps() {
            stage?.capFps = GeodeNative.surfaceFpsChoice(settings.fps).toFloat()
        }

        /** The app's saved look (LookStore), as it is; the MilkDrop preset is the stage's own (core/viz/MilkMemory). */
        private fun restoreLook(engine: Stage) {
            val looks = LookStore(prefsFiles, contentResolver)
            looks.pushProcessWide()
            engine.look = looks.current()
            applyPerformance(engine)
        }

        /**
         * Settings › Performance's quality; its fps cap is the live visual's, and the wallpaper paces
         * to its own choice instead (applyFps, ADR 0008). A new store reads the file afresh.
         */
        private fun applyPerformance(engine: Stage) {
            engine.performance = PerformanceStore(generalPrefs, this@VisualizerWallpaperService).settings.value
        }

        /**
         * Restates what the engine sees to the stage, which applies the native run rule
         * (core/viz/SurfaceRun): its pacing, a still frame's settle and what a stop sheds. Called
         * on every change that can move it: visibility, surface, cover, battery saver, unlock,
         * silence, Remove animations, and a trial's end.
         */
        private fun syncRunState() {
            val active = stage ?: return
            // The rule is native (core/viz/SurfaceRun); this only gathers what the engine sees. The
            // entitlement is re-read on every change, so a purchase or a trial made in the app
            // animates the wallpaper the next time the home screen shows, and the native rule says
            // when a running trial's end needs a restate, since nothing announces it (#376).
            val now = WallClock.SYSTEM.nowMs()
            val until = ProSnapshot.animateUntilMs(this@VisualizerWallpaperService, ProFeature.LIVE_WALLPAPER)
            val conditions =
                WallpaperConditions(
                    surfaceAvailable = surfaceAvailable,
                    visible = visible,
                    preview = isPreview,
                    covered = covered,
                    batterySaver = powerManager?.isPowerSaveMode == true,
                    silent = sound.silent,
                    reducedMotion = systemAnimations.removed,
                    settings = settings,
                    animateUntilMs = until,
                )
            handler.removeCallbacks(trialEnded)
            conditions.recheckInMs(now)?.let { handler.postDelayed(trialEnded, it) }
            syncListening(conditions.listening(now))
            pushGlow(glow.copy(enabled = currentLook() == WallpaperLook.EDGE))
            active.setSurface(conditions.flags, until, now)
        }

        private fun pushGlow(next: EdgeGlow) {
            if (next == glow) return
            glow = next
            stage?.setEdgeGlow(next.toFloatArray())
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
            if (!visible) stage?.submitTouchPoints(touchPoints, 0)
            // Re-read each time it shows: the options page, opened from the picker, covers it.
            if (visible) {
                settings = prefsStore.load()
                applyFps()
            }
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
            val frame = holder.surfaceFrame
            stage?.setWindow(holder.surface, frame.width(), frame.height())
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
            stage?.setSize(width, height)
            updateVisibleWindow()
        }

        override fun onSurfaceRedrawNeeded(holder: SurfaceHolder) {
            super.onSurfaceRedrawNeeded(holder)
            stage?.redraw()
        }

        /**
         * The launcher's pan, so the edge look draws on the screen's edge rather than the
         * surface's. Passed on raw; `core/viz/frame/WallpaperWindow.hpp` makes the window of it.
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
            offsets[0] = xOffset
            offsets[1] = yOffset
            offsets[2] = xOffsetStep
            offsets[3] = yOffsetStep
            offsets[4] = xPixels.toFloat()
            offsets[5] = yPixels.toFloat()
            updateVisibleWindow()
        }

        private fun updateVisibleWindow() {
            val metrics = resources.displayMetrics
            val displayWidth = metrics.widthPixels.toFloat()
            val displayHeight = metrics.heightPixels.toFloat()
            GeodeNative.wallpaperWindow(surfaceWidth, surfaceHeight, displayWidth, displayHeight, offsets, window)
            pushGlow(glow.copy(left = window[0], top = window[1], width = window[2], height = window[3]))
        }

        /**
         * The wallpaper's whole input: publish the live pointers, in the same y-up NDC the
         * app publishes, so a scene behaves identically in both hosts.
         *
         * There is no pinch here on purpose. It writes to the visual params, and
         * a wallpaper has no controls to undo them with — a stray pinch on the home screen
         * would leave the wallpaper zoomed with no way back. Publishing pointers is
         * transient by construction: it decays to nothing the moment the finger leaves.
         */
        override fun onTouchEvent(event: MotionEvent) {
            super.onTouchEvent(event)
            val engine = stage ?: return
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
            // Returns once the stage has let go of the surface, which is gone after this callback.
            stage?.setWindow(null, 0, 0)
            super.onSurfaceDestroyed(holder)
        }

        override fun onDestroy() {
            handler.removeCallbacks(trialEnded)
            sound.stop()
            systemAnimations.stop()
            generalPrefs.unregisterOnSharedPreferenceChangeListener(performanceChanged)
            bestEffort(TAG, "unregister system receiver") { unregisterReceiver(systemReceiver) }
            // A wallpaper engine is made and destroyed every time the screen sleeps or the launcher
            // switches screens, so each one ends its stage (its thread, context and renderer) here.
            stage?.close()
            stage = null
            super.onDestroy()
        }
    }

    private companion object {
        const val TAG = "VisualizerWallpaper"

        /** Wallpaper zoom (0 none, 1 fully zoomed out) past which the shade or recents covers it. */
        const val COVER_ZOOM = 0.9f
    }
}
