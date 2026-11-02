package dev.geode.wallpaper

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.service.dreams.DreamService
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.core.content.ContextCompat
import dev.geode.billing.ProSnapshot
import dev.geode.data.BehaviourStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceStore
import dev.geode.data.PresetStore
import dev.geode.data.ThemeStore
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerRenderer
import dev.geode.render.VisualizerView
import dev.geode.util.bestEffort

/**
 * The visualizer as a screensaver (Daydream): the same renderer [VisualizerWallpaperService]
 * hosts, on a plain window instead of a wallpaper surface, with the time and what is playing
 * over it ([DreamOverlay], each part a toggle in [DreamPrefsStore]).
 *
 * Reusing [VisualizerView] itself — rather than wiring a bare `GLSurfaceView` the way the
 * wallpaper has to — is what a Dream's window buys over a wallpaper's: it is a real window with a
 * real view hierarchy, so [VisualizerView]'s own attach/detach and window-visibility pacing, and
 * its own scene teardown on `onDetachedFromWindow`, all apply completely unmodified.
 *
 * It runs by the wallpaper's rule ([wallpaperRunMode]), because it is the same thing on the same
 * kind of screen — one that sits there for hours while nobody may be looking:
 * - covered ([DreamCover]: the proximity sensor reads near, face down or in a pocket): no frames;
 * - battery saver: a still frame;
 * - silence ([SoundSource]'s grace): a still frame, until sound comes back.
 * A dream has no Settings page of its own (`res/xml/dream_info.xml`), so those two options are
 * on, the battery-friendly defaults of [WallpaperSettings]. It hears what the wallpaper hears:
 * the app's ring while MuzViz feeds it, else any app's audio once the user has granted the
 * permission the output-mix capture needs.
 */
class VisualizerDreamService : DreamService() {
    private var visualizerView: VisualizerView? = null
    private var overlay: DreamOverlay? = null

    private val handler = Handler(Looper.getMainLooper())
    private val powerManager by lazy { getSystemService(PowerManager::class.java) }

    /** The wallpaper's options, minus the looks: a dream has no options screen to change them. */
    private val settings = WallpaperSettings()

    private val sound = SoundSource(this) { syncRunState() }

    /** Lazy: it asks for the sensor service, which a service has no context for until onCreate. */
    private val cover by lazy { DreamCover(this) { syncRunState() } }

    /** Between onDreamingStarted and onDreamingStopped: the only time anyone can see it. */
    private var dreaming = false
    private var listening = false
    private var glPaused = false
    private var runMode = WallpaperRunMode.STOPPED

    /** A still frame's settle burst is running; see [syncRunState]. */
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
        if (!ProSnapshot.gate(this).allows(ProFeature.SCREENSAVER)) {
            finish()
            return
        }
        isInteractive = false
        isFullscreen = true
        val prefsFiles = GeodePrefsFiles(this)
        // Its own switch (Settings › Behaviour), no longer Player's keep-screen-on.
        isScreenBright = BehaviourStore(prefsFiles.general).load().dreamScreenBright
        val view = VisualizerView(this)
        visualizerView = view
        restoreLiveState(view.visualizerRenderer, prefsFiles)
        view.applyPerformance(PerformanceStore(prefsFiles.general, this).settings.value)
        view.visualizerRenderer.pcmProvider = { null }
        view.visualizerRenderer.soundInput = { sound.input }
        val stage = DreamStage(this)
        stage.addView(view, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        val shown = DreamPrefsStore(this).load()
        if (shown.showClock || shown.showNowPlaying) {
            val dreamOverlay = DreamOverlay(this, shown.showClock, shown.showNowPlaying)
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

    override fun onDetachedFromWindow() {
        handler.removeCallbacks(endStillSettle)
        sound.stop()
        cover.stop()
        overlay?.stop()
        if (receiverRegistered) {
            bestEffort(TAG, "unregister power receiver") { unregisterReceiver(powerReceiver) }
            receiverRegistered = false
        }
        // Scene teardown is VisualizerView's own onDetachedFromWindow, called by super below.
        (visualizerView?.parent as? ViewGroup)?.removeView(visualizerView)
        visualizerView = null
        overlay = null
        super.onDetachedFromWindow()
    }

    /**
     * Restates the run mode from scratch, as the wallpaper's engine does: on dreaming, cover,
     * battery saver, silence and window visibility.
     *
     * [VisualizerView] starts its own pacer whenever its window shows or it resumes, which is right
     * for the app and wrong for a still frame, so a still frame that is not settling stops the
     * pacer again on every restate rather than trusting it stayed stopped.
     */
    private fun syncRunState() {
        val view = visualizerView ?: return
        val conditions =
            WallpaperConditions(
                surfaceAvailable = true,
                visible = dreaming,
                // Checked once in onAttachedToWindow: without Pro there is no dream to run.
                animationAllowed = true,
                preview = false,
                covered = cover.covered,
                batterySaver = powerManager?.isPowerSaveMode == true,
                silent = false,
                settings = settings,
            )
        // Listen whenever the dream would animate if there were sound, cover aside: that is what
        // wakes a resting dream, and lifting the phone for a second should not restart the
        // silence clock.
        syncListening(wallpaperRunMode(conditions.copy(covered = false)) == WallpaperRunMode.ANIMATED)
        val previous = runMode
        runMode = wallpaperRunMode(conditions.copy(silent = sound.silent))
        if (runMode != WallpaperRunMode.STILL) {
            handler.removeCallbacks(endStillSettle)
            settling = false
        }
        when (runMode) {
            WallpaperRunMode.ANIMATED -> {
                resumeGl(view)
                view.framePacer.start()
            }
            WallpaperRunMode.STILL -> {
                resumeGl(view)
                if (previous != WallpaperRunMode.STILL) {
                    // Paced for a moment first: a scene is built across frames (ScenePrep), so one
                    // frame on a fresh renderer would hold a black or half-built picture.
                    settling = true
                    view.framePacer.start()
                    handler.postDelayed(endStillSettle, STILL_SETTLE_MS)
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

    /** Mirrors [VisualizerWallpaperService]'s own `restoreLiveState`: the same prefs, the same keys. */
    private fun restoreLiveState(
        engine: VisualizerRenderer,
        prefsFiles: GeodePrefsFiles,
    ) {
        val prefs = prefsFiles.viz
        prefs.getString("live_state", null)?.let { json ->
            runCatching { PresetStore.fromJson(json) }.getOrNull()?.let { preset ->
                engine.requestedSceneId = preset.sceneId
                engine.sceneParams = preset.params
            }
        }
        // The MilkDrop preset needs nothing here: the renderer restores the app's (MilkPresetMemory).
        val gui = ThemeStore(prefsFiles.general).loadGui()
        // The gentle flight (owner, wave 1): the camera keeps flying but without big moves, the
        // same calm the reduce-motion setting gives, because a dream is watched from across a room.
        engine.reducedMotion = true
        engine.soundTuning = gui.soundTuning
        // The user's modulation too: without it the wallpaper and the dream drew the look
        // with every LFO, envelope and custom reaction switched off.
        val modulation = dev.geode.data.LfoStore(prefsFiles.general)
        engine.lfoEngine.configs = modulation.load()
        engine.adsrEngine.configs = modulation.loadAdsrs()
        engine.reactions = modulation.loadReactions()
    }

    private companion object {
        const val TAG = "VisualizerDream"

        /** How long a still frame keeps drawing first, so a scene being built finishes on screen. */
        const val STILL_SETTLE_MS = 2_000L
    }
}
