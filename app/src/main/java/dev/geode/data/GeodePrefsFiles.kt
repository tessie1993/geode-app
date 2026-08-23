package dev.geode.data

import android.content.Context
import android.content.SharedPreferences

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

    private fun open(name: String): SharedPreferences = appContext.getSharedPreferences(name, Context.MODE_PRIVATE)

    companion object {
        const val GENERAL = "geode-prefs"
        const val VIZ = "geode-viz"
        const val PLAYER = "geode-player"
        const val FAVOURITES = "geode-favourites"
        const val AUDIO_FX = "geode-audiofx"
        const val MODULATION = "geode-mod"
        const val LIBRARY = "geode-library"
    }
}
