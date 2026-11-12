package dev.geode.data

import android.content.ContentResolver
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

/**
 * The look as the app last saved it (#345), for the surfaces that run without the app's session: the
 * wallpaper, the dream and Cast. It reads the same keys the session writes (`data/VizStateStore` for the
 * style, params and transition; [SoundInputSettingsCodec] for Settings › Audio, [SystemMotion] for reduce motion; [LfoStore] for the
 * modulation and the user's reactions), so those surfaces can never draw a look the app does not.
 *
 * Here and not in `ui`, so `wallpaper/` and `cast/` read it without crossing the layer rule; that is
 * why the wallpaper and the dream used to carry their own copies with the raw `live_state` key.
 * The background image is not loaded yet: it needs a decode at the surface's size, which only the
 * live stage does today.
 */
class LookStore(
    private val prefsFiles: GeodePrefsFiles,
    private val resolver: ContentResolver,
) : LookSource {
    override fun current(): LookSnapshot {
        val viz = prefsFiles.viz
        val live = viz.getString(KEY_LIVE_STATE, null)?.let { runCatching { PresetStore.fromJson(it) }.getOrNull() }
        val soundTuning = SoundInputSettingsCodec.readSoundTuning(prefsFiles.general)
        val modulation = LfoStore(prefsFiles.general)
        val defaults = LookSnapshot()
        return LookSnapshot(
            sceneId = live?.sceneId ?: defaults.sceneId,
            params = live?.params ?: defaults.params,
            lfos = modulation.load(),
            adsrs = modulation.loadAdsrs(),
            reactions = modulation.loadReactions(),
            soundTuning = soundTuning,
            reducedMotion = SystemMotion.isReduced(resolver),
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
        val modulation = LfoStore(prefsFiles.general)
        modulation.pushHand(modulation.loadHandMask(), modulation.loadReactions() != null)
        // The saved Customize › Layers layer, read here on the caller's thread so the first frame draws it
        // (the container's LayerStore pushes the same line once its own read is back).
        bestEffort(TAG, "push saved layer") { GeodeNative.layerSet(LayerCodec().read(prefsFiles.general)) }
        // Seam: once #498 lands, the saved custom shader code (geode_shader_override_set), a process-wide
        // native store nothing pushes from the stores when the process starts without UI.
    }

    internal companion object {
        private const val TAG = "LookStore"

        // The session's keys (VizStateStore writes them); one spelling, here below `ui`.
        const val KEY_LIVE_STATE = "live_state"
        const val KEY_TRANSITION_ID = "transition_id"
        const val KEY_TRANSITION_DURATION_SEC = "transition_duration_sec"
        private const val MS_PER_SECOND = 1000

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.VIZ) {
                portable(PrefType.STRING, KEY_LIVE_STATE, KEY_TRANSITION_ID)
                portable(PrefType.FLOAT, KEY_TRANSITION_DURATION_SEC)
            }
    }
}
