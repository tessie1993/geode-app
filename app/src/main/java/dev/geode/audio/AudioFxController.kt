package dev.geode.audio

import android.content.SharedPreferences
import dev.geode.audio.dsp.DspSettings
import dev.geode.audio.dsp.NativeDspProcessor

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
 * Drives the native equalizer chain and remembers its settings.
 *
 * The chain lives inside the player's processor list, so it is always "attached"; [attach] is
 * kept for the audio-session callers and only records the id.
 */
class AudioFxController(
    private val prefs: SharedPreferences,
    private val presets: List<AudioFxPreset>,
    private val dsp: NativeDspProcessor,
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

    val bandCount: Int get() = NativeDspProcessor.BANDS

    fun bandRange(band: Int): Pair<Int, Int> {
        require(band in 0 until bandCount) { "band $band" }
        return NativeDspProcessor.MIN_MB to NativeDspProcessor.MAX_MB
    }

    fun setBandLevel(
        band: Int,
        mB: Int,
    ): Boolean {
        if (band !in 0 until bandCount) return false
        val (lo, hi) = bandRange(band)
        val levels = dsp.settings.bandsMb.toMutableList()
        levels[band] = mB.coerceIn(lo, hi)
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
        val s = strength.coerceIn(0, 1000)
        dsp.update(dsp.settings.copy(bassBoost = s))
        prefs.edit().putInt(KEY_BASS, s).apply()
        return true
    }

    fun setLoudness(mB: Int): Boolean {
        val g = mB.coerceIn(0, 1000)
        dsp.update(dsp.settings.copy(loudnessMb = g))
        prefs.edit().putInt(KEY_LOUDNESS, g).apply()
        return true
    }

    /** The ReplayGain / preamp stage, in decibels; owned by playback, not by the equalizer settings. */
    fun setGainDb(db: Float) = dsp.update(dsp.settings.copy(gainDb = db))

    fun setCrossfeed(enabled: Boolean) = dsp.update(dsp.settings.copy(crossfeed = enabled))

    fun setLimiter(enabled: Boolean) = dsp.update(dsp.settings.copy(limiter = enabled))

    fun snapshot(): AudioFxState {
        val s = dsp.settings
        val (lo, hi) = NativeDspProcessor.MIN_MB to NativeDspProcessor.MAX_MB
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
        )
    }

    private fun restored(): DspSettings {
        val preset = prefs.getInt(KEY_PRESET, -1)
        val fromPreset = presets.getOrNull(preset)?.levelsMb
        val stored = AudioFxFormat.decodeBandLevels(prefs.getString(KEY_BANDS, null))
        val levels =
            List(bandCount) { band ->
                (fromPreset?.getOrNull(band) ?: stored.getOrNull(band) ?: 0).coerceIn(NativeDspProcessor.MIN_MB, NativeDspProcessor.MAX_MB)
            }
        return dsp.settings.copy(
            enabled = prefs.getBoolean(KEY_ENABLED, false),
            bandsMb = levels,
            bassBoost = prefs.getInt(KEY_BASS, 0).coerceIn(0, 1000),
            loudnessMb = prefs.getInt(KEY_LOUDNESS, 0).coerceIn(0, 1000),
        )
    }

    private companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_BANDS = "band_levels"
        const val KEY_PRESET = "preset"
        const val KEY_BASS = "bass"
        const val KEY_LOUDNESS = "loudness"
    }
}
