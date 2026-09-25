package dev.geode.wallpaper

import android.service.dreams.DreamService
import android.view.ViewGroup
import dev.geode.audio.AudioBus
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.MotionPrefs
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.PresetStore
import dev.geode.render.VisualizerRenderer
import dev.geode.render.VisualizerView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * The visualizer as a screensaver (Daydream): the same renderer [VisualizerWallpaperService]
 * hosts, on a plain window instead of a wallpaper surface.
 *
 * Reusing [VisualizerView] itself — rather than wiring a bare `GLSurfaceView` the way the
 * wallpaper has to — is what a Dream's window buys over a wallpaper's: it is a real window with a
 * real view hierarchy, so [VisualizerView]'s own attach/detach and window-visibility pacing, and
 * its own scene teardown on `onDetachedFromWindow`, all apply completely unmodified — nothing
 * here re-implements them. Only the audio feed — [AudioBus.features] when something is playing,
 * an idle drift otherwise — has to be supplied, for the same reason [VisualizerWallpaperService]
 * supplies it too: a Dream, like a wallpaper, has no player state of its own to read it from.
 */
class VisualizerDreamService : DreamService() {
    private var visualizerView: VisualizerView? = null
    private val idle = IdleFeatures()
    private var lastFrameMs = 0L
    private var feeder: Job? = null
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    @Volatile
    private var running = false

    /**
     * See [VisualizerWallpaperService]'s own `feedGeneration` for why this exists: a bounded
     * `join` on teardown can time out, and without this the old thread would have no way to learn
     * that a newer run — or none at all — has taken over, and would keep feeding a renderer that
     * is either someone else's or already gone.
     */
    @Volatile
    private var feedGeneration = 0

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        isInteractive = false
        isFullscreen = true
        val prefsFiles = GeodePrefsFiles(this)
        // No screensaver-specific brightness setting exists to read; this is the closest existing
        // analogue — the same "keep the screen on" preference the app's own fullscreen visualizer
        // honours, for the same reason: a moving image on a dimmed screen looks broken, not calm.
        isScreenBright = PlayerPrefsStore(prefsFiles.player).load().keepScreenOn
        val view = VisualizerView(this)
        visualizerView = view
        restoreLiveState(view.visualizerRenderer, prefsFiles)
        view.visualizerRenderer.pcmProvider = { null }
        setContentView(view)
    }

    override fun onDreamingStarted() {
        super.onDreamingStarted()
        visualizerView?.visualizerRenderer?.let(::startFeeding)
    }

    override fun onDreamingStopped() {
        stopFeeding()
        super.onDreamingStopped()
    }

    override fun onDetachedFromWindow() {
        // Belt and braces: onDreamingStopped is documented to run first, but a feeder thread left
        // running past window teardown would keep this service's process alive to feed a renderer
        // no scene will ever read from again. Scene teardown itself is not repeated here — that is
        // VisualizerView's own onDetachedFromWindow, called by super below.
        stopFeeding()
        (visualizerView?.parent as? ViewGroup)?.removeView(visualizerView)
        visualizerView = null
        super.onDetachedFromWindow()
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
        prefs.getString("milk_path", null)?.let { path ->
            if (java.io.File(path).isFile) engine.loadMilkPreset(path)
        }
        engine.reducedMotion = MotionPrefs.reducedMotion(prefsFiles.general)
    }

    private fun startFeeding(engine: VisualizerRenderer) {
        if (feeder != null) return
        AudioBus.addConsumer()
        val generation = ++feedGeneration
        running = true
        lastFrameMs = android.os.SystemClock.elapsedRealtime()
        feeder =
            scope.launch {
                while (running && feedGeneration == generation) {
                    val now = android.os.SystemClock.elapsedRealtime()
                    val dt = ((now - lastFrameMs).coerceIn(1, 100)) / 1000f
                    lastFrameMs = now
                    engine.features = AudioBus.features() ?: idle.tick(dt)
                    delay(FEED_INTERVAL_MS)
                }
            }
    }

    private fun stopFeeding() {
        val job = feeder ?: return
        running = false
        feedGeneration++
        AudioBus.removeConsumer()
        job.cancel()
        feeder = null
    }

    private companion object {
        const val TAG = "VisualizerDream"
        const val FEED_INTERVAL_MS = 16L
        const val FEEDER_JOIN_MS = 200L
    }
}
