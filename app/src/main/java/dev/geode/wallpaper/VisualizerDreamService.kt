package dev.geode.wallpaper

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.service.dreams.DreamService
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.core.content.ContextCompat
import dev.geode.billing.ProSnapshot
import dev.geode.billing.WallClock
import dev.geode.data.BehaviourStore
import dev.geode.data.LookStore
import dev.geode.data.PerformanceStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.pro.ProFeature
import dev.geode.render.FrameRatePolicy
import dev.geode.render.VisualizerView
import dev.geode.util.bestEffort

/**
 * The visualizer as a screensaver (Daydream): the same renderer [VisualizerWallpaperService]
 * hosts, on a plain window instead of a wallpaper surface, with the time and what is playing
 * over it ([DreamOverlay], each part a Settings › Wallpaper & screen saver toggle).
 *
 * Reusing [VisualizerView] itself — rather than wiring a bare `GLSurfaceView` the way the
 * wallpaper has to — is what a Dream's window buys over a wallpaper's: it is a real window with a
 * real view hierarchy, so [VisualizerView]'s own attach/detach and window-visibility pacing, and
 * its own scene teardown on `onDetachedFromWindow`, all apply completely unmodified.
 *
 * It runs by the wallpaper's rule (native: [WallpaperConditions.runMode]), because it is the same
 * thing on the same kind of screen — one that sits there for hours while nobody may be looking:
 * - covered ([DreamCover]: the proximity sensor reads near, face down or in a pocket): no frames;
 * - battery saver: a still frame;
 * - silence ([SoundSource]'s grace): no visual, faded to black under the overlay, until sound comes
 *   back (owner: no sound, no visuals; #999);
 * - reduced motion (the system's Remove animations, [SystemAnimations]): a
 *   still frame for the whole dream (owner, ADR 0008; #389). Off, the dream keeps its gentle flight.
 * It paces to its own fps choice, Behaviour › Dream ([dev.geode.data.BehaviourPrefs.dreamFps]),
 * not to Settings › Performance's cap, which is the live visual's.
 * A dream has no Settings page of its own (`res/xml/dream_info.xml`), so those two options are
 * on, the battery-friendly defaults of [WallpaperSettings]. It hears the app's ring while
 * BeatRealms feeds it, else, when Settings › Wallpaper & screen saver says so
 * ([dev.geode.data.BehaviourPrefs.dreamHearOtherApps]) and the permission the output-mix capture
 * needs is granted, any app's audio, as the wallpaper's "React to any app" does.
 */
class VisualizerDreamService : DreamService() {
    private var visualizerView: VisualizerView? = null
    private var overlay: DreamOverlay? = null

    private val handler = Handler(Looper.getMainLooper())
    private val powerManager by lazy { getSystemService(PowerManager::class.java) }

    /**
     * The wallpaper's options, minus the looks: a dream has no options screen to change them. Other
     * apps' sound is Behaviour's choice, set at attach; it was a fixed default (off) before, so the
     * dream stood still whenever another app played.
     */
    private var settings = WallpaperSettings()

    private val sound = SoundSource(this) { syncRunState() }

    /** Lazy: a service has no context for its prefs until onCreate. */
    private val generalPrefs by lazy { geodeContainer.prefsFiles.general }

    /** Behaviour's dream frame rate, read at attach; [applyPerformance] restates it after the cap. */
    private var dreamFps = GeodeNative.SURFACE_DEFAULT_FPS

    /**
     * Settings › Performance, followed live like the wallpaper's: the app shares this process and
     * the prefs file, so a change (a backup restored mid-dream, say) reaches the running dream.
     * A field: listeners are held weakly.
     */
    private val performanceChanged =
        SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key in PerformanceStore.KEYS) applyPerformance()
        }

    /** Lazy: it asks for the sensor service, which a service has no context for until onCreate. */
    private val cover by lazy { DreamCover(this) { syncRunState() } }

    /** Lazy for the same reason: it reads the application context. A flip restates at once. */
    private val systemAnimations by lazy { SystemAnimations(this) { syncRunState() } }

    /** Between onDreamingStarted and onDreamingStopped: the only time anyone can see it. */
    private var dreaming = false
    private var listening = false
    private var glPaused = false
    private var runMode = WallpaperRunMode.STOPPED

    /** Black over the visual while the run rule says [WallpaperRunMode.DARK]; the overlay stays above. */
    private var darkCover: View? = null
    private var darkShown = false

    /** A still frame's settle burst is running; how long is native ([settleMsFrom]). */
    private var settling = false
    private val endStillSettle =
        Runnable {
            settling = false
            if (runMode == WallpaperRunMode.STILL) visualizerView?.framePacer?.stop()
        }

    private val powerReceiver =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context,
                intent: Intent,
            ) = syncRunState()
        }
    private var receiverRegistered = false

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        // Pro feature: without it the dream ends at once and the system falls back to its own.
        if (!entitled()) {
            finish()
            return
        }
        isInteractive = false
        isFullscreen = true
        val prefsFiles = geodeContainer.prefsFiles
        // Its own switch (Settings › Wallpaper & screen saver), no longer Player's keep-screen-on.
        val behaviour = BehaviourStore(prefsFiles.general).load()
        isScreenBright = behaviour.dreamScreenBright
        settings = WallpaperSettings(hearOtherApps = behaviour.dreamHearOtherApps)
        val view = VisualizerView(this)
        visualizerView = view
        // The app's saved look in the gentle flight (owner, wave 1): the camera keeps flying without big
        // moves, because a dream is watched from across a room. The MilkDrop preset is the renderer's own.
        val looks = LookStore(prefsFiles, contentResolver)
        looks.pushProcessWide()
        view.visualizerRenderer.look = looks.current().gentleFlight()
        dreamFps = behaviour.dreamFps
        applyPerformance()
        view.visualizerRenderer.soundInput = { sound.input }
        val stage = DreamStage(this)
        stage.addView(view, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        // Above the GL view (a SurfaceView's hole would clear anything below it), under the overlay.
        val black = View(this).apply { setBackgroundColor(android.graphics.Color.BLACK) }
        black.alpha = 0f
        darkCover = black
        stage.addView(black, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        if (behaviour.dreamShowClock || behaviour.dreamShowNowPlaying) {
            val dreamOverlay = DreamOverlay(this, behaviour.dreamShowClock, behaviour.dreamShowNowPlaying)
            overlay = dreamOverlay
            stage.addView(
                dreamOverlay,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.CENTER,
                ),
            )
        }
        setContentView(stage)
        ContextCompat.registerReceiver(
            this,
            powerReceiver,
            IntentFilter(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        receiverRegistered = true
        systemAnimations.start()
        generalPrefs.registerOnSharedPreferenceChangeListener(performanceChanged)
    }

    /**
     * Settings › Performance's quality on the dream's view, then the dream's own frame rate: the
     * view's applyPerformance caps to the live visual's fps, and the dream paces to its own choice,
     * snapped natively. The view hands the rate to its surface when that is created.
     */
    private fun applyPerformance() {
        val view = visualizerView ?: return
        view.applyPerformance(PerformanceStore(generalPrefs, this).settings.value)
        view.framePacer.policy = FrameRatePolicy.Capped(GeodeNative.surfaceFpsChoice(dreamFps).toFloat())
        // applyPerformance just asked the surface for the live cap (up to 120); ask again for the dream's own,
        // since the pacer re-asks only when the cap it paces to moves, and that stays the dream's.
        view.holder.surface?.takeIf { it.isValid }?.let(view.framePacer::applyTo)
    }

    override fun onDreamingStarted() {
        super.onDreamingStarted()
        dreaming = true
        cover.start()
        overlay?.start()
        syncRunState()
    }

    override fun onDreamingStopped() {
        dreaming = false
        cover.stop()
        overlay?.stop()
        syncRunState()
        super.onDreamingStopped()
    }

    /** Restates when a running trial ends; the native rule says when (#376). */
    private val trialEnded = Runnable { syncRunState() }

    /** The entitlement as the native rule reads it: Pro forever, a trial its end, free 0. */
    private fun animateUntilMs(): Long = ProSnapshot.animateUntilMs(this, ProFeature.SCREENSAVER)

    private fun entitled(): Boolean = GeodeNative.surfaceEntitled(animateUntilMs(), WallClock.SYSTEM.nowMs())

    override fun onDetachedFromWindow() {
        generalPrefs.unregisterOnSharedPreferenceChangeListener(performanceChanged)
        handler.removeCallbacks(endStillSettle)
        handler.removeCallbacks(trialEnded)
        sound.stop()
        cover.stop()
        systemAnimations.stop()
        overlay?.stop()
        if (receiverRegistered) {
            bestEffort(TAG, "unregister power receiver") { unregisterReceiver(powerReceiver) }
            receiverRegistered = false
        }
        // Scene teardown is VisualizerView's own onDetachedFromWindow, called by super below.
        (visualizerView?.parent as? ViewGroup)?.removeView(visualizerView)
        visualizerView = null
        overlay = null
        darkCover?.animate()?.cancel()
        darkCover = null
        super.onDetachedFromWindow()
    }

    /**
     * Restates the run mode from scratch, as the wallpaper's engine does: on dreaming, cover,
     * battery saver, silence, Remove animations and window visibility.
     *
     * [VisualizerView] starts its own pacer whenever its window shows or it resumes, which is right
     * for the app and wrong for a still frame, so a still frame that is not settling stops the
     * pacer again on every restate rather than trusting it stayed stopped.
     */
    private fun syncRunState() {
        val view = visualizerView ?: return
        val now = WallClock.SYSTEM.nowMs()
        val conditions =
            WallpaperConditions(
                surfaceAvailable = true,
                visible = dreaming,
                preview = false,
                covered = cover.covered,
                batterySaver = powerManager?.isPowerSaveMode == true,
                silent = sound.silent,
                reducedMotion = systemAnimations.removed,
                settings = settings,
                animateUntilMs = animateUntilMs(),
                darkWhenSilent = true,
            )
        // A trial that ends mid-dream ends the dream, the same as a free user starting one
        // (owner, #376), and Android falls back to its own.
        if (!conditions.entitled(now)) {
            finish()
            return
        }
        handler.removeCallbacks(trialEnded)
        conditions.recheckInMs(now)?.let { handler.postDelayed(trialEnded, it) }
        // The native rule says when to listen: whenever the dream would move with sound, cover
        // aside, so lifting the phone for a second does not restart the silence clock.
        syncListening(conditions.listening(now))
        val previous = runMode
        runMode = conditions.runMode(now)
        if (runMode != WallpaperRunMode.STILL) {
            handler.removeCallbacks(endStillSettle)
            settling = false
        }
        showDark(runMode == WallpaperRunMode.DARK)
        when (runMode) {
            // No frames under the black; the GL view is not paused, so sound wakes it at once.
            WallpaperRunMode.DARK -> view.framePacer.stop()
            WallpaperRunMode.ANIMATED -> {
                resumeGl(view)
                view.framePacer.start()
            }
            WallpaperRunMode.STILL -> {
                resumeGl(view)
                val settle = runMode.settleMsFrom(previous)
                if (settle > 0) {
                    // Paced for a moment first, as long as the native rule says: a scene is built
                    // across frames (ScenePrep), so one frame on a fresh renderer would hold a black
                    // or half-built picture.
                    settling = true
                    view.framePacer.start()
                    handler.postDelayed(endStillSettle, settle)
                } else if (!settling) {
                    view.framePacer.stop()
                    view.requestRender()
                }
            }
            WallpaperRunMode.STOPPED -> {
                view.framePacer.stop()
                if (!glPaused) {
                    view.onPause()
                    glPaused = true
                }
            }
        }
    }

    /**
     * Fades the black over the visual in or out on a change only. The platform's long animation time
     * is scaled by the system's animator duration scale, so Remove animations makes it a cut.
     */
    private fun showDark(dark: Boolean) {
        if (dark == darkShown) return
        darkShown = dark
        darkCover
            ?.animate()
            ?.alpha(if (dark) 1f else 0f)
            ?.setDuration(resources.getInteger(android.R.integer.config_longAnimTime).toLong())
    }

    private fun resumeGl(view: VisualizerView) {
        if (!glPaused) return
        view.onResume()
        glPaused = false
    }

    /** A start counts as fresh sound, so [sound] restarts only on a real change. */
    private fun syncListening(listen: Boolean) {
        if (listen == listening) return
        listening = listen
        if (listen) sound.start(settings.hearOtherApps) else sound.stop()
    }

    /**
     * The dream's root. [VisualizerView] restarts its pacer when its window shows again; restating
     * the run mode just after (posted, so the child has had its own callback) puts a still or
     * covered dream back to rest.
     */
    private inner class DreamStage(
        context: Context,
    ) : FrameLayout(context) {
        override fun onWindowVisibilityChanged(visibility: Int) {
            super.onWindowVisibilityChanged(visibility)
            handler.post { syncRunState() }
        }
    }

    private companion object {
        const val TAG = "VisualizerDream"
    }
}
