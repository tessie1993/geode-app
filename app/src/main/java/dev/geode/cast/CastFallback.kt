package dev.geode.cast

import android.content.Context
import dev.geode.audio.AudioBus
import dev.geode.data.LookSnapshot
import dev.geode.data.PerformanceSettings
import dev.geode.engine.bridge.StageNative
import dev.geode.viz.NativeViz

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
    private val viz = NativeViz(appContext)

    /** What it draws; only the parts that are new objects are pushed. */
    var look: LookSnapshot = LookSnapshot()
        set(value) {
            field = value
            viz.setLook(value)
        }

    init {
        viz.setLook(look)
        PerformanceSettings.HIGH.run { viz.setQuality(renderScale, marchScale, postFx) }
        viz.surfaceCreated()
        viz.surfaceChanged(width, height)
    }

    /**
     * One frame. The rate handed over is the encoder's, not the stage's cap: at 30 this renderer is keeping up,
     * so its fluid monitor must not read its frames against the phone's 60 (or 120).
     */
    fun draw() {
        val fps = CastStream.FPS.toFloat()
        val events = viz.frame(AudioBus.heard, fps, fps)
        if (events and StageNative.FRAME_DIRECTOR_MOMENT != 0 && viz.takeDirectorStyleMoments() > 0) CastMirror.directorMoment()
    }

    fun release() = viz.destroy()
}
