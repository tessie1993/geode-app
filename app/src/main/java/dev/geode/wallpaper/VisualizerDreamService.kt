package dev.geode.wallpaper

import android.service.dreams.DreamService
import android.view.ViewGroup
import dev.geode.audio.AudioBus
import dev.geode.billing.ProSnapshot
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.PerformanceStore
import dev.geode.data.PlayerPrefsStore
import dev.geode.data.PresetStore
import dev.geode.data.ThemeStore
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerRenderer
import dev.geode.render.VisualizerView

/**
 * The visualizer as a screensaver (Daydream): the same renderer [VisualizerWallpaperService]
 * hosts, on a plain window instead of a wallpaper surface.
 *
 * Reusing [VisualizerView] itself — rather than wiring a bare `GLSurfaceView` the way the
 * wallpaper has to — is what a Dream's window buys over a wallpaper's: it is a real window with a
 * real view hierarchy, so [VisualizerView]'s own attach/detach and window-visibility pacing, and
 * its own scene teardown on `onDetachedFromWindow`, all apply completely unmodified — nothing
 * here re-implements them. Its sound engine hears the playback session's ring through
 * [AudioBus.soundInput], as the wallpaper's does: a Dream has no `PlayerViewModel` to read it from.
 */
class VisualizerDreamService : DreamService() {
    private var visualizerView: VisualizerView? = null

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
        // No screensaver-specific brightness setting exists to read; this is the closest existing
        // analogue — the same "keep the screen on" preference the app's own fullscreen visualizer
        // honours, for the same reason: a moving image on a dimmed screen looks broken, not calm.
        isScreenBright = PlayerPrefsStore(prefsFiles.player).load().keepScreenOn
        val view = VisualizerView(this)
        visualizerView = view
        restoreLiveState(view.visualizerRenderer, prefsFiles)
        view.applyPerformance(PerformanceStore(prefsFiles.general, this).settings.value)
        view.visualizerRenderer.pcmProvider = { null }
        view.visualizerRenderer.soundInput = { AudioBus.soundInput }
        setContentView(view)
    }

    override fun onDetachedFromWindow() {
        // Scene teardown is VisualizerView's own onDetachedFromWindow, called by super below.
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
        val gui = ThemeStore(prefsFiles.general).loadGui()
        engine.reducedMotion = gui.reducedMotion
        engine.soundTuning = gui.soundTuning
        // The user's modulation too: without it the wallpaper and the dream drew the look
        // with every LFO, envelope and custom reaction switched off.
        val modulation = dev.geode.data.LfoStore(prefsFiles.general)
        engine.lfoEngine.configs = modulation.load()
        engine.adsrEngine.configs = modulation.loadAdsrs()
        engine.reactions = modulation.loadReactions()
    }
}
