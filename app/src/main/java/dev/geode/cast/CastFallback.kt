package dev.geode.cast

import android.content.Context
import dev.geode.audio.AudioBus
import dev.geode.data.MilkPathPrefs
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.render.PerformanceSettings
import dev.geode.render.bridge.NativeViz
import dev.geode.render.look.LookSnapshot
import java.io.File

/**
 * The renderer a cast draws with while the app's stage does not: straight into the encoder's own EGL context,
 * on the cast's render thread, at the encoder's fixed rate. Every call is on that thread, its context current.
 */
internal class CastFallback(
    context: Context,
    width: Int,
    height: Int,
) {
    private val appContext = context.applicationContext
    private val prefs = appContext.geodeContainer.prefsFiles.viz
    private val viz = NativeViz(appContext)

    /** What it draws; only the parts that are new objects are pushed. */
    var look: LookSnapshot = LookSnapshot()
        set(value) {
            field = value
            viz.setLook(value)
        }

    init {
        MilkPathPrefs.seed(prefs)
        // A new renderer starts on the preset the stage showed last, not on MilkDrop's idle one.
        viz.onCreated = {
            GeodeNative.milkMemoryRestorePath(appContext.assets, File(appContext.filesDir, "milk").path)?.let(viz::loadMilkPreset)
        }
        viz.setLook(look)
        viz.setQuality(PerformanceSettings.HIGH)
        viz.surfaceCreated()
        viz.surfaceChanged(width, height)
    }

    fun loadMilkPreset(path: String) {
        GeodeNative.milkMemoryRequest(path)
        viz.loadMilkPreset(path)
    }

    /**
     * One frame. The rate handed over is the encoder's, not the stage's cap: at 30 this renderer is keeping up,
     * so its fluid monitor and the shared thermal trend must not read its frames against the phone's 60 (or 120).
     */
    fun draw() {
        val fps = CastEncoder.FPS.toFloat()
        val events = viz.frame(AudioBus.soundInput, fps, fps)
        if (events and GeodeNative.FRAME_MILK_LOADED != 0) {
            viz.takeMilkPresetLoaded()?.let { if (GeodeNative.milkMemoryLoaded(it)) MilkPathPrefs.save(prefs, it) }
        }
        if (events and GeodeNative.FRAME_DIRECTOR_MOMENT != 0 && viz.takeDirectorStyleMoments() > 0) CastMirror.directorMoment()
    }

    fun release() = viz.destroy()
}
