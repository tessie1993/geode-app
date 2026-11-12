package dev.geode.data

import android.content.Context
import android.content.SharedPreferences
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys

/**
 * The registry of the app's SharedPreferences files: every file the app keeps is named here, and
 * one instance is built per process, by `GeodeContainer` (read it as `context.geodeContainer.prefsFiles`).
 * Outside `data/` and the container nothing opens a prefs file or builds this class
 * (`tools/layer-gate.sh`), so a later change of storage or of who owns a file has one place to go
 * (#1009, PR 2).
 */
class GeodePrefsFiles(
    context: Context,
) {
    private val appContext = context.applicationContext

    val general: SharedPreferences by lazy { open(GENERAL) }
    val viz: SharedPreferences by lazy { open(VIZ) }
    val player: SharedPreferences by lazy { open(PLAYER) }
    val favourites: SharedPreferences by lazy { open(FAVOURITES) }
    val audioFx: SharedPreferences by lazy { open(AUDIO_FX) }
    val modulation: SharedPreferences by lazy { open(MODULATION) }
    val library: SharedPreferences by lazy { open(LIBRARY) }
    val background: SharedPreferences by lazy { open(BACKGROUND) }

    /** The live wallpaper's own options (`wallpaper/WallpaperPrefsStore`). */
    val wallpaper: SharedPreferences by lazy { open(WALLPAPER) }

    /** The home screen widget's cache (`widget/WidgetState`). */
    val widget: SharedPreferences by lazy { open(WIDGET) }

    /** The Pro purchase cache and the trial (`billing/EntitlementStore`, `billing/StoredTrialRepository`). */
    val pro: SharedPreferences by lazy { open(PRO) }

    private fun open(name: String): SharedPreferences = appContext.getSharedPreferences(name, Context.MODE_PRIVATE)

    companion object {
        const val GENERAL = "geode-prefs"
        const val VIZ = "geode-viz"
        const val PLAYER = "geode-player"
        const val FAVOURITES = "geode-favourites"
        const val AUDIO_FX = "geode-audiofx"
        const val MODULATION = "geode-mod"
        const val LIBRARY = "geode-library"
        const val BACKGROUND = "geode-background"
        const val WALLPAPER = "geode-wallpaper"
        const val WIDGET = "geode-widget"
        const val PRO = "geode-pro"

        /**
         * Record's ask-once save target. `RecordSaveStore` opens it itself (it is in `data/` and
         * needs the context for the folder grant anyway), so there is no property for it here.
         */
        const val RECORD = "geode-record"

        /** The shown MilkDrop preset, which [MilkPathPrefs] reads and writes. */
        internal val MILK_PRESET_PREF_KEYS =
            prefKeys(VIZ) {
                portable(PrefType.STRING, MilkPathPrefs.KEY)
            }
    }
}
