package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.MicCapture
import dev.geode.data.VisualsSettingsCodec
import dev.geode.engine.bridge.GeodeNative
import kotlin.math.roundToInt

/** Sound reaction › Basics: the microphone, a layer over whatever plays. */
@Composable
internal fun LiveInputRow(viewModel: PlayerViewModel) {
    CrystalOverline(stringResource(R.string.source_live_input), color = MaterialTheme.colorScheme.onSurfaceVariant)
    LiveInputGroup(viewModel)
}

/** Sound reaction › Basics: other apps' audio, and naming what they play. */
@Composable
internal fun OtherAppsAudioRow(viewModel: PlayerViewModel) {
    CrystalOverline(stringResource(R.string.source_other_apps), color = MaterialTheme.colorScheme.onSurfaceVariant)
    ExternalAudioSettings(viewModel)
}

/** Sound reaction › Analysis: the sound engine's tuning. */
@Composable
internal fun SoundAnalysisSection(viewModel: SettingsViewModel) {
    val input by viewModel.soundInput.collectAsStateWithLifecycle()
    Column {
        SoundTuningControls(input.soundTuning) { viewModel.updateSoundInput { s -> s.copy(soundTuning = it) } }
    }
}

/**
 * Visual playback › Preset morph: how long one look glides into the next when a preset is applied
 * ([dev.geode.data.VisualsSettings.presetMorphSeconds]); it moved from Sound input with the regroup, since
 * it is how the visual changes, not how it hears.
 */
@Composable
internal fun PresetMorphRow(viewModel: SettingsViewModel) {
    val visuals by viewModel.visuals.collectAsStateWithLifecycle()
    Column {
        CrystalSlider(
            label = stringResource(R.string.audio_preset_morph, visuals.presetMorphSeconds),
            labelStyle = LocalTextStyle.current,
            value = visuals.presetMorphSeconds,
            onValueChange = { viewModel.updateVisuals { s -> s.copy(presetMorphSeconds = it) } },
            valueRange = 0f..VisualsSettingsCodec.PRESET_MORPH_SECONDS_MAX,
        )
    }
}

@Composable
private fun LiveInputGroup(viewModel: PlayerViewModel) {
    val mic by viewModel.micState.collectAsStateWithLifecycle()
    val micPermission =
        rememberPermissionRequest(android.Manifest.permission.RECORD_AUDIO) { viewModel.setMicEnabled(true) }
    Column {
        MicToggleRow(
            active = mic.active,
            onToggle = { want ->
                if (!want) {
                    viewModel.setMicEnabled(false)
                } else if (viewModel.hasMicPermission()) {
                    viewModel.setMicEnabled(true)
                } else {
                    micPermission.request()
                }
            },
        )
        MicStatusNotice(micPermission, mic.failure)
    }
}

@Composable
private fun MicToggleRow(
    active: Boolean,
    onToggle: (Boolean) -> Unit,
) {
    LabeledSwitch(
        stringResource(R.string.audio_react_mic),
        checked = active,
        onCheckedChange = onToggle,
    )
    Text(
        stringResource(R.string.audio_react_mic_explainer),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** Why live input is off, when it is, and the way back to Settings once Android stops asking. */
@Composable
private fun MicStatusNotice(
    permission: PermissionRequestState,
    failure: MicCapture.Failure?,
) {
    when {
        permission.refused || failure == MicCapture.Failure.PERMISSION -> {
            Text(
                stringResource(R.string.audio_mic_denied),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
            )
            if (permission.status == PermissionStatus.PERMANENTLY_DENIED) {
                CrystalButton(onClick = permission::openSettings, compact = true, filled = false) {
                    Text(stringResource(R.string.permission_open_settings))
                }
            }
        }
        failure == MicCapture.Failure.UNAVAILABLE ->
            Text(
                stringResource(R.string.audio_mic_busy),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
            )
    }
}

/**
 * The sound engine's tuning (core/sound GeodeSoundTuning): Reactivity and Speed up front, the
 * TouchDesigner-style per-band shaping, the drums and the event thresholds under Advanced. Global,
 * so the screen, the wallpaper, the dream and every export hear the music the same way.
 */
@Composable
private fun SoundTuningControls(
    saved: List<Float>?,
    onChange: (List<Float>?) -> Unit,
) {
    var advanced by rememberSaveable { mutableStateOf(false) }
    val tuning = remember(saved) { TuningSlots(saved) }
    Column {
        val reactivity = GeodeNative.SOUND_TUNING_REACTIVITY
        TuningSlot(stringResource(R.string.audio_reactivity, "%.2f".format(tuning[reactivity])), tuning, reactivity, onChange)
        val speed = GeodeNative.SOUND_TUNING_SPEED
        TuningSlot(stringResource(R.string.audio_speed, "%.2f".format(tuning[speed])), tuning, speed, onChange)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(filled = false, onClick = { advanced = !advanced }) { Text(stringResource(R.string.audio_advanced)) }
            TextButton(onClick = { onChange(null) }) { Text(stringResource(R.string.audio_default)) }
        }
        if (advanced) AdvancedTuning(tuning, onChange)
    }
}

/** A saved tuning as the sliders read it: every slot as the engine hears it, over native's ranges. */
private class TuningSlots(
    saved: List<Float>?,
) {
    private val schema = checkNotNull(GeodeNative.soundTuningSchema())
    private val values = checkNotNull(GeodeNative.soundTuningResolve(saved?.toFloatArray()))

    operator fun get(slot: Int): Float = values[slot]

    fun range(slot: Int): ClosedFloatingPointRange<Float> {
        val n = GeodeNative.SOUND_TUNING_FLOATS
        return schema[n + slot]..schema[2 * n + slot]
    }

    /** What to save with [slot] moved to [value], clamped by native. */
    fun with(
        slot: Int,
        value: Float,
    ): List<Float> = checkNotNull(GeodeNative.soundTuningResolve(values.copyOf().also { it[slot] = value })).toList()
}

/** Seconds as whole milliseconds, for the attack, release, decay and hold labels. */
private fun ms(seconds: Float) = (seconds * 1000).roundToInt()

@Composable
private fun AdvancedTuning(
    tuning: TuningSlots,
    onChange: (List<Float>?) -> Unit,
) {
    val bands = listOf(R.string.audio_band_low, R.string.audio_band_middle, R.string.audio_band_high)
    bands.forEachIndexed { i, name ->
        Text(stringResource(name), style = MaterialTheme.typography.titleSmall)
        val gain = GeodeNative.SOUND_TUNING_GAIN + i
        val threshold = GeodeNative.SOUND_TUNING_THRESHOLD + i
        val attack = GeodeNative.SOUND_TUNING_ATTACK + i
        val release = GeodeNative.SOUND_TUNING_RELEASE + i
        TuningSlot(stringResource(R.string.audio_band_gain, "%.2f".format(tuning[gain])), tuning, gain, onChange)
        TuningSlot(stringResource(R.string.audio_band_threshold, "%.2f".format(tuning[threshold])), tuning, threshold, onChange)
        TuningSlot(stringResource(R.string.audio_band_attack, ms(tuning[attack])), tuning, attack, onChange)
        TuningSlot(stringResource(R.string.audio_band_release, ms(tuning[release])), tuning, release, onChange)
    }
    Text(stringResource(R.string.audio_drums), style = MaterialTheme.typography.titleSmall)
    listOf(R.string.audio_drum_kick, R.string.audio_drum_snare, R.string.audio_drum_hat).forEachIndexed { i, label ->
        val drum = GeodeNative.SOUND_TUNING_DRUM + i
        val decay = GeodeNative.SOUND_TUNING_DRUM_DECAY + i
        val drumLine = stringResource(label, "%.2f".format(tuning[drum]), ms(tuning[decay]))
        TuningSlot(drumLine, tuning, drum, onChange)
        CrystalSlider(
            valueDescription = drumLine,
            value = tuning[decay],
            onValueChange = { onChange(tuning.with(decay, it)) },
            valueRange = tuning.range(decay),
        )
    }
    Text(stringResource(R.string.audio_events), style = MaterialTheme.typography.titleSmall)
    val onset = GeodeNative.SOUND_TUNING_ONSET_THRESHOLD
    TuningSlot(stringResource(R.string.audio_onset_threshold, "%.1f".format(tuning[onset])), tuning, onset, onChange)
    val cut = GeodeNative.SOUND_TUNING_HARD_CUT_LEVEL
    TuningSlot(stringResource(R.string.audio_hard_cut_level, "%.1f".format(tuning[cut])), tuning, cut, onChange)
    val halfLife = GeodeNative.SOUND_TUNING_HARD_CUT_HALF_LIFE
    TuningSlot(stringResource(R.string.audio_hard_cut_half_life, tuning[halfLife].roundToInt()), tuning, halfLife, onChange)
    val pitch = GeodeNative.SOUND_TUNING_PITCH_THRESHOLD
    TuningSlot(stringResource(R.string.audio_pitch_threshold, "%.2f".format(tuning[pitch])), tuning, pitch, onChange)
    val hold = GeodeNative.SOUND_TUNING_NOTE_HOLD
    TuningSlot(stringResource(R.string.audio_note_hold, ms(tuning[hold])), tuning, hold, onChange)
    val silence = GeodeNative.SOUND_TUNING_SILENCE_DB
    TuningSlot(stringResource(R.string.audio_silence, tuning[silence].roundToInt()), tuning, silence, onChange)
    val trim = GeodeNative.SOUND_TUNING_INPUT_TRIM_DB
    TuningSlot(stringResource(R.string.audio_input_trim, "%+.0f".format(tuning[trim])), tuning, trim, onChange)
}

/** One tuning slot's slider, over the range native gives that slot. */
@Composable
private fun TuningSlot(
    label: String,
    tuning: TuningSlots,
    slot: Int,
    onChange: (List<Float>?) -> Unit,
) = TuningSlider(label, tuning[slot], tuning.range(slot)) { onChange(tuning.with(slot, it)) }

@Composable
private fun TuningSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
) {
    CrystalSlider(value = value, onValueChange = onChange, valueRange = range, label = label)
}
