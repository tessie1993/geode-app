package dev.geode.audio

import android.content.Context
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative

/** The built-in equalizer curves as native lists them (core/audio/dsp/EqPresets), named here in its order. */
object AudioFxPresets {
    private val names =
        intArrayOf(
            R.string.eq_preset_flat,
            R.string.eq_preset_bass,
            R.string.eq_preset_treble,
            R.string.eq_preset_vocal,
            R.string.eq_preset_rock,
            R.string.eq_preset_electronic,
        )

    fun all(context: Context): List<AudioFxPreset> {
        val levels = IntArray(GeodeNative.dspBandCount())
        return List(minOf(names.size, GeodeNative.dspEqPresetCount())) { index ->
            GeodeNative.dspEqPresetLevels(index, levels)
            AudioFxPreset(context.getString(names[index]), levels.toList())
        }
    }
}
