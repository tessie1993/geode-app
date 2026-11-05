package dev.geode.data

import dev.geode.render.HandMask
import dev.geode.render.look.LookSnapshot
import dev.geode.render.look.LookSource

/**
 * The look as the app last saved it (#345), for the surfaces that run without the app's session: the
 * wallpaper, the dream and Cast. It reads the same keys the session writes (`ui/VizStateStore` for the
 * style, params and transition; [ThemeStore] for Settings › Audio and reduce motion; [LfoStore] for the
 * modulation and the user's reactions), so those surfaces can never draw a look the app does not.
 *
 * Here and not in `ui`, so `wallpaper/` and `cast/` read it without crossing the layer rule; that is
 * why the wallpaper and the dream used to carry their own copies with the raw `live_state` key.
 * The background image is not loaded yet: it needs a decode at the surface's size, which only the
 * live stage does today.
 */
class LookStore(
    private val prefsFiles: GeodePrefsFiles,
) : LookSource {
    override fun current(): LookSnapshot {
        val viz = prefsFiles.viz
        val live = viz.getString(KEY_LIVE_STATE, null)?.let { runCatching { PresetStore.fromJson(it) }.getOrNull() }
        val gui = ThemeStore(prefsFiles.general).loadGui()
        val modulation = LfoStore(prefsFiles.general)
        val defaults = LookSnapshot()
        return LookSnapshot(
            sceneId = live?.sceneId ?: defaults.sceneId,
            params = live?.params ?: defaults.params,
            lfos = modulation.load(),
            adsrs = modulation.loadAdsrs(),
            reactions = modulation.loadReactions(),
            soundTuning = gui.soundTuning,
            reducedMotion = gui.reducedMotion,
            transitionId = viz.getString(KEY_TRANSITION_ID, null) ?: defaults.transitionId,
            transitionMs =
                if (viz.contains(KEY_TRANSITION_DURATION_SEC)) {
                    (viz.getFloat(KEY_TRANSITION_DURATION_SEC, 0f) * MS_PER_SECOND).toLong()
                } else {
                    defaults.transitionMs
                },
        )
    }

    /**
     * Pushes the process-wide native state a renderer draws under, for a process the app's session never
     * started (the wallpaper or the dream after a kill). The app's own controllers push it themselves.
     */
    fun pushProcessWide() {
        HandMask.set(LfoStore(prefsFiles.general).loadHandMask())
        // Seam: once #602 lands, push the saved Customize › Layers layer here (GeodeNative.layerSet);
        // once #498 lands, the saved custom shader code (geode_shader_override_set). Both are process-wide
        // native stores that nothing pushes from the stores when the process starts without UI.
    }

    internal companion object {
        // The session's keys (VizStateStore writes them); one spelling, here below `ui`.
        const val KEY_LIVE_STATE = "live_state"
        const val KEY_TRANSITION_ID = "transition_id"
        const val KEY_TRANSITION_DURATION_SEC = "transition_duration_sec"
        private const val MS_PER_SECOND = 1000
    }
}
