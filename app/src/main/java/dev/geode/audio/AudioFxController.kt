package dev.geode.audio

import android.content.SharedPreferences
import dev.geode.audio.dsp.DspSettings
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.engine.bridge.GeodeNative

data class AudioFxBand(
    val label: String,
    val levelMb: Int,
    val minMb: Int,
    val maxMb: Int,
)

data class AudioFxState(
    val available: Boolean = false,
    val attached: Boolean = false,
    val bassAvailable: Boolean = false,
    val loudnessAvailable: Boolean = false,
    val enabled: Boolean = false,
    val bands: List<AudioFxBand> = emptyList(),
    val presets: List<String> = emptyList(),
    val presetIndex: Int = -1,
    val bassBoost: Int = 0,
    val loudness: Int = 0,
    val bassMax: Int = 0,
    val loudnessMax: Int = 0,
)

object AudioFxFormat {
    fun freqLabel(milliHz: Int): String {
        val hz = milliHz / 1000
        if (hz < 1000) return "$hz Hz"
        val whole = hz / 1000
        val tenth = (hz % 1000) / 100
        return if (tenth == 0) "$whole kHz" else "$whole.$tenth kHz"
    }

    fun dbLabel(mB: Int): String {
        val abs = if (mB < 0) -mB else mB
        val whole = abs / 100
        val tenth = (abs % 100) / 10
        val num = if (tenth == 0) "$whole" else "$whole.$tenth"
        return when {
            mB > 0 -> "+$num dB"
            mB < 0 -> "-$num dB"
            else -> "0 dB"
        }
    }

    fun encodeBandLevels(levels: List<Int>): String = levels.joinToString(",")

    fun decodeBandLevels(csv: String?): List<Int> =
        csv
            ?.split(',')
            ?.mapNotNull { it.trim().toIntOrNull() }
            .orEmpty()
}

/** A named set of band levels in millibels, low band first. */
data class AudioFxPreset(
    val name: String,
    val levelsMb: List<Int>,
)

/**
 * The effects' ranges, clamps and saved-levels resolve. They are native (`core/audio/dsp/FxRanges`); the
 * interface is only the seam JVM tests, which have no libgeode, hand a stand-in through.
 */
interface AudioFxLimits {
    val bandCount: Int

    fun min(control: Int): Int

    fun max(control: Int): Int

    fun clamp(
        control: Int,
        value: Int,
    ): Int

    fun restoreBands(
        preset: Int,
        stored: List<Int>,
    ): List<Int>

    object Native : AudioFxLimits {
        override val bandCount: Int get() = GeodeNative.dspBandCount()

        override fun min(control: Int): Int = GeodeNative.dspFxMin(control)

        override fun max(control: Int): Int = GeodeNative.dspFxMax(control)

        override fun clamp(
            control: Int,
            value: Int,
        ): Int = GeodeNative.dspFxClamp(control, value)

        override fun restoreBands(
            preset: Int,
            stored: List<Int>,
        ): List<Int> = IntArray(bandCount).also { GeodeNative.dspRestoreBands(preset, stored.toIntArray(), it) }.toList()
    }
}

/**
 * Drives the native equalizer chain and remembers its settings.
 *
 * The chain lives inside the player's processor list, so it is always "attached"; [attach] is
 * kept for the audio-session callers and only records the id.
 */
class AudioFxController(
    private val prefs: SharedPreferences,
    private val presets: List<AudioFxPreset>,
    private val dsp: NativeDspProcessor,
    private val limits: AudioFxLimits = AudioFxLimits.Native,
) {
    private var sessionId: Int = 0

    val available: Boolean get() = true

    val attached: Boolean get() = true

    init {
        dsp.update(restored())
    }

    fun attach(sessionId: Int) {
        this.sessionId = sessionId
    }

    fun release() {
        sessionId = 0
    }

    fun setEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_ENABLED, enabled).apply()
        dsp.update(dsp.settings.copy(enabled = enabled))
    }

    fun setBandLevel(
        band: Int,
        mB: Int,
    ): Boolean {
        val levels = dsp.settings.bandsMb.toMutableList()
        if (band !in levels.indices) return false
        levels[band] = limits.clamp(GeodeNative.DSP_FX_BAND, mB)
        dsp.update(dsp.settings.copy(bandsMb = levels))
        prefs
            .edit()
            .putInt(KEY_PRESET, -1)
            .putString(KEY_BANDS, AudioFxFormat.encodeBandLevels(levels))
            .apply()
        return true
    }

    val presetNames: List<String> get() = presets.map { it.name }

    fun usePreset(i: Int): Boolean {
        val preset = presets.getOrNull(i) ?: return false
        dsp.update(dsp.settings.copy(bandsMb = preset.levelsMb))
        prefs
            .edit()
            .putInt(KEY_PRESET, i)
            .putString(KEY_BANDS, AudioFxFormat.encodeBandLevels(preset.levelsMb))
            .apply()
        return true
    }

    fun setBassBoost(strength: Int): Boolean {
        val s = limits.clamp(GeodeNative.DSP_FX_BASS_BOOST, strength)
        dsp.update(dsp.settings.copy(bassBoost = s))
        prefs.edit().putInt(KEY_BASS, s).apply()
        return true
    }

    fun setLoudness(mB: Int): Boolean {
        val g = limits.clamp(GeodeNative.DSP_FX_LOUDNESS, mB)
        dsp.update(dsp.settings.copy(loudnessMb = g))
        prefs.edit().putInt(KEY_LOUDNESS, g).apply()
        return true
    }

    fun snapshot(): AudioFxState {
        val s = dsp.settings
        val lo = limits.min(GeodeNative.DSP_FX_BAND)
        val hi = limits.max(GeodeNative.DSP_FX_BAND)
        return AudioFxState(
            available = true,
            attached = true,
            bassAvailable = true,
            loudnessAvailable = true,
            enabled = s.enabled,
            bands =
                s.bandsMb.mapIndexed { band, mb ->
                    AudioFxBand(
                        label = AudioFxFormat.freqLabel(NativeDspProcessor.bandCenterMilliHz(band)),
                        levelMb = mb,
                        minMb = lo,
                        maxMb = hi,
                    )
                },
            presets = presetNames,
            presetIndex = prefs.getInt(KEY_PRESET, -1),
            bassBoost = s.bassBoost,
            loudness = s.loudnessMb,
            bassMax = limits.max(GeodeNative.DSP_FX_BASS_BOOST),
            loudnessMax = limits.max(GeodeNative.DSP_FX_LOUDNESS),
        )
    }

    private fun restored(): DspSettings {
        val stored = AudioFxFormat.decodeBandLevels(prefs.getString(KEY_BANDS, null))
        return dsp.settings.copy(
            enabled = prefs.getBoolean(KEY_ENABLED, false),
            bandsMb = limits.restoreBands(prefs.getInt(KEY_PRESET, -1), stored),
            bassBoost = limits.clamp(GeodeNative.DSP_FX_BASS_BOOST, prefs.getInt(KEY_BASS, 0)),
            loudnessMb = limits.clamp(GeodeNative.DSP_FX_LOUDNESS, prefs.getInt(KEY_LOUDNESS, 0)),
        )
    }

    internal companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_BANDS = "band_levels"
        const val KEY_PRESET = "preset"
        const val KEY_BASS = "bass"
        const val KEY_LOUDNESS = "loudness"

        val PREF_KEYS =
            prefKeys(GeodePrefsFiles.AUDIO_FX) {
                portable(PrefType.BOOLEAN, KEY_ENABLED)
                portable(PrefType.STRING, KEY_BANDS)
                portable(PrefType.INT, KEY_PRESET, KEY_BASS, KEY_LOUDNESS)
            }
    }
}
