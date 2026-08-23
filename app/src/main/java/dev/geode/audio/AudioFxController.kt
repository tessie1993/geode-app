package dev.geode.audio

import android.content.SharedPreferences
import android.media.audiofx.BassBoost
import android.media.audiofx.Equalizer
import android.media.audiofx.LoudnessEnhancer
import android.util.Log
import dev.geode.util.bestEffort

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

class AudioFxController(
    private val prefs: SharedPreferences,
) {
    private var equalizer: Equalizer? = null
    private var bassBoost: BassBoost? = null
    private var loudness: LoudnessEnhancer? = null
    private var sessionId: Int = 0

    val available: Boolean
        get() = equalizer != null

    val attached: Boolean
        get() = sessionId > 0

    fun attach(sessionId: Int) {
        if (sessionId == this.sessionId && equalizer != null) return
        release()
        this.sessionId = sessionId
        if (sessionId <= 0) return
        equalizer = runCatching { Equalizer(0, sessionId) }.getOrNull()
        bassBoost = runCatching { BassBoost(0, sessionId) }.getOrNull()
        loudness = runCatching { LoudnessEnhancer(sessionId) }.getOrNull()
        restore()
    }

    fun release() {
        bestEffort(TAG, "equalizer release") { equalizer?.release() }
        bestEffort(TAG, "bassBoost release") { bassBoost?.release() }
        bestEffort(TAG, "loudness release") { loudness?.release() }
        equalizer = null
        bassBoost = null
        loudness = null
        sessionId = 0
    }

    fun setEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_ENABLED, enabled).apply()
        applyEnabled(enabled)
    }

    val bandCount: Int
        get() = equalizer?.let { eq -> runCatching { eq.numberOfBands.toInt() }.getOrNull() } ?: 0

    fun bandRange(band: Int): Pair<Int, Int> =
        equalizer
            ?.let { eq -> runCatching { eq.bandLevelRange.let { it[0].toInt() to it[1].toInt() } }.getOrNull() }
            ?: (-1500 to 1500)

    fun setBandLevel(
        band: Int,
        mB: Int,
    ): Boolean {
        val eq = equalizer ?: return false
        val (lo, hi) = bandRange(band)
        val applied =
            runCatching { eq.setBandLevel(band.toShort(), mB.coerceIn(lo, hi).toShort()) }
                .onFailure { Log.w(TAG, "setBandLevel($band) failed", it) }
                .isSuccess
        if (applied) {
            prefs
                .edit()
                .putInt(KEY_PRESET, -1)
                .putString(KEY_BANDS, AudioFxFormat.encodeBandLevels(currentBandLevels()))
                .apply()
        }
        return applied
    }

    val presetNames: List<String>
        get() =
            equalizer
                ?.let { eq ->
                    runCatching {
                        (0 until eq.numberOfPresets.toInt()).map { eq.getPresetName(it.toShort()) }
                    }.getOrNull()
                }.orEmpty()

    fun usePreset(i: Int): Boolean {
        val eq = equalizer
        if (eq == null || i !in presetNames.indices) return false
        val applied =
            runCatching { eq.usePreset(i.toShort()) }
                .onFailure { Log.w(TAG, "usePreset($i) failed", it) }
                .isSuccess
        if (applied) {
            prefs
                .edit()
                .putInt(KEY_PRESET, i)
                .putString(KEY_BANDS, AudioFxFormat.encodeBandLevels(currentBandLevels()))
                .apply()
        }
        return applied
    }

    fun setBassBoost(strength: Int): Boolean {
        val s = strength.coerceIn(0, 1000)
        val applied =
            runCatching { bassBoost?.setStrength(s.toShort()) }
                .onFailure { Log.w(TAG, "setBassBoost failed", it) }
                .isSuccess
        if (applied) prefs.edit().putInt(KEY_BASS, s).apply()
        return applied
    }

    fun setLoudness(mB: Int): Boolean {
        val g = mB.coerceIn(0, 1000)
        val applied =
            runCatching { loudness?.setTargetGain(g) }
                .onFailure { Log.w(TAG, "setLoudness failed", it) }
                .isSuccess
        if (applied) prefs.edit().putInt(KEY_LOUDNESS, g).apply()
        return applied
    }

    fun snapshot(): AudioFxState {
        val base =
            AudioFxState(
                available = available,
                attached = attached,
                bassAvailable = bassBoost != null,
                loudnessAvailable = loudness != null,
                enabled = prefs.getBoolean(KEY_ENABLED, false),
                presetIndex = prefs.getInt(KEY_PRESET, -1),
                bassBoost = prefs.getInt(KEY_BASS, 0),
                loudness = prefs.getInt(KEY_LOUDNESS, 0),
            )
        val eq = equalizer ?: return base
        val bands =
            runCatching {
                val range = eq.bandLevelRange
                (0 until eq.numberOfBands.toInt()).map { b ->
                    AudioFxBand(
                        label = AudioFxFormat.freqLabel(eq.getCenterFreq(b.toShort())),
                        levelMb = eq.getBandLevel(b.toShort()).toInt(),
                        minMb = range[0].toInt(),
                        maxMb = range[1].toInt(),
                    )
                }
            }.getOrDefault(emptyList())
        return base.copy(bands = bands, presets = presetNames)
    }

    private fun currentBandLevels(): List<Int> =
        equalizer
            ?.let { eq ->
                runCatching {
                    (0 until eq.numberOfBands.toInt()).map { eq.getBandLevel(it.toShort()).toInt() }
                }.getOrNull()
            }.orEmpty()

    private fun restore() {
        val eq = equalizer
        if (eq != null) {
            val preset = prefs.getInt(KEY_PRESET, -1)
            val presetApplied = preset >= 0 && runCatching { eq.usePreset(preset.toShort()) }.isSuccess
            if (!presetApplied) {
                val levels = AudioFxFormat.decodeBandLevels(prefs.getString(KEY_BANDS, null))
                levels.forEachIndexed { band, mb ->
                    if (band < bandCount) {
                        val (lo, hi) = bandRange(band)
                        bestEffort(TAG, "restore band $band") {
                            eq.setBandLevel(band.toShort(), mb.coerceIn(lo, hi).toShort())
                        }
                    }
                }
            }
        }
        bestEffort(TAG, "restore bassBoost") {
            bassBoost?.setStrength(prefs.getInt(KEY_BASS, 0).coerceIn(0, 1000).toShort())
        }
        bestEffort(TAG, "restore loudness") {
            loudness?.setTargetGain(prefs.getInt(KEY_LOUDNESS, 0).coerceIn(0, 1000))
        }
        applyEnabled(prefs.getBoolean(KEY_ENABLED, false))
    }

    private fun applyEnabled(enabled: Boolean) {
        bestEffort(TAG, "equalizer enabled=$enabled") { equalizer?.enabled = enabled }
        bestEffort(TAG, "bassBoost enabled=$enabled") { bassBoost?.enabled = enabled }
        bestEffort(TAG, "loudness enabled=$enabled") { loudness?.enabled = enabled }
    }

    private companion object {
        const val KEY_ENABLED = "enabled"
        const val KEY_BANDS = "band_levels"
        const val KEY_PRESET = "preset"
        const val KEY_BASS = "bass"
        const val KEY_LOUDNESS = "loudness"
    }
}

private const val TAG = "AudioFxController"
