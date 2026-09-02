package dev.geode.audio

import android.content.Context
import dev.geode.R

/** The built-in equalizer curves, ten bands from 31 Hz to 16 kHz in millibels. */
object AudioFxPresets {
    fun all(context: Context): List<AudioFxPreset> =
        listOf(
            AudioFxPreset(context.getString(R.string.eq_preset_flat), List(10) { 0 }),
            AudioFxPreset(context.getString(R.string.eq_preset_bass), listOf(600, 500, 350, 150, 0, 0, 0, 0, 0, 0)),
            AudioFxPreset(context.getString(R.string.eq_preset_treble), listOf(0, 0, 0, 0, 0, 100, 250, 400, 500, 550)),
            AudioFxPreset(context.getString(R.string.eq_preset_vocal), listOf(-200, -150, -50, 100, 300, 350, 300, 150, 0, -100)),
            AudioFxPreset(context.getString(R.string.eq_preset_rock), listOf(450, 350, 150, -50, -150, -50, 150, 300, 400, 450)),
            AudioFxPreset(context.getString(R.string.eq_preset_electronic), listOf(500, 400, 100, 0, -150, 0, 150, 300, 450, 500)),
        )
}
