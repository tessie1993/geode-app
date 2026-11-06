package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.MicCapture
import dev.geode.data.ThemeStore
import dev.geode.engine.audio.SoundTuning
import kotlin.math.roundToInt

/** Settings › Sound input › Basics: the microphone, then other apps' audio. */
@Composable
internal fun SoundInputBasics(viewModel: PlayerViewModel) {
    CrystalOverline(stringResource(R.string.source_live_input), color = MaterialTheme.colorScheme.onSurfaceVariant)
    LiveInputGroup(viewModel)
    CrystalOverline(stringResource(R.string.source_other_apps), color = MaterialTheme.colorScheme.onSurfaceVariant)
    ExternalAudioSettings(viewModel)
}

/** Settings › Sound input › How the visual listens: the sound engine's tuning and the preset morph. */
@Composable
internal fun SoundAnalysisSection(viewModel: SettingsViewModel) {
    AnalysisGroup(viewModel)
}

@Composable
private fun AnalysisGroup(viewModel: SettingsViewModel) {
    val gui by viewModel.guiPrefs.collectAsStateWithLifecycle()
    Column {
        SoundTuningControls(gui.soundTuning) { viewModel.setGuiPrefs(gui.copy(soundTuning = it.clamped())) }
    }
    Column {
        Text(stringResource(R.string.audio_preset_morph, gui.presetMorphSeconds))
        CrystalSlider(
            value = gui.presetMorphSeconds,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(presetMorphSeconds = it)) },
            valueRange = 0f..ThemeStore.PRESET_MORPH_SECONDS_MAX,
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
    tuning: SoundTuning,
    onChange: (SoundTuning) -> Unit,
) {
    var advanced by rememberSaveable { mutableStateOf(false) }
    Column {
        val reactivity = tuning[SoundTuning.REACTIVITY]
        TuningSlot(stringResource(R.string.audio_reactivity, "%.2f".format(reactivity)), tuning, SoundTuning.REACTIVITY, onChange)
        TuningSlot(stringResource(R.string.audio_speed, "%.2f".format(tuning[SoundTuning.SPEED])), tuning, SoundTuning.SPEED, onChange)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(filled = false, onClick = { advanced = !advanced }) { Text(stringResource(R.string.audio_advanced)) }
            TextButton(onClick = { onChange(SoundTuning()) }) { Text(stringResource(R.string.audio_default)) }
        }
        if (advanced) AdvancedTuning(tuning, onChange)
    }
}

/** Seconds as whole milliseconds, for the attack, release, decay and hold labels. */
private fun ms(seconds: Float) = (seconds * 1000).roundToInt()

@Composable
private fun AdvancedTuning(
    tuning: SoundTuning,
    onChange: (SoundTuning) -> Unit,
) {
    val bands = listOf(R.string.audio_band_low, R.string.audio_band_middle, R.string.audio_band_high)
    bands.forEachIndexed { i, name ->
        Text(stringResource(name), style = MaterialTheme.typography.titleSmall)
        val gain = SoundTuning.GAIN + i
        val threshold = SoundTuning.THRESHOLD + i
        val attack = SoundTuning.ATTACK + i
        val release = SoundTuning.RELEASE + i
        TuningSlot(stringResource(R.string.audio_band_gain, "%.2f".format(tuning[gain])), tuning, gain, onChange)
        TuningSlot(stringResource(R.string.audio_band_threshold, "%.2f".format(tuning[threshold])), tuning, threshold, onChange)
        TuningSlot(stringResource(R.string.audio_band_attack, ms(tuning[attack])), tuning, attack, onChange)
        TuningSlot(stringResource(R.string.audio_band_release, ms(tuning[release])), tuning, release, onChange)
    }
    Text(stringResource(R.string.audio_drums), style = MaterialTheme.typography.titleSmall)
    listOf(R.string.audio_drum_kick, R.string.audio_drum_snare, R.string.audio_drum_hat).forEachIndexed { i, label ->
        val drum = SoundTuning.DRUM + i
        val decay = SoundTuning.DRUM_DECAY + i
        TuningSlot(stringResource(label, "%.2f".format(tuning[drum]), ms(tuning[decay])), tuning, drum, onChange)
        CrystalSlider(
            value = tuning[decay],
            onValueChange = { onChange(tuning.withSlot(decay, it)) },
            valueRange = SoundTuning.range(decay),
        )
    }
    Text(stringResource(R.string.audio_events), style = MaterialTheme.typography.titleSmall)
    val onset = SoundTuning.ONSET_THRESHOLD
    TuningSlot(stringResource(R.string.audio_onset_threshold, "%.1f".format(tuning[onset])), tuning, onset, onChange)
    val cut = SoundTuning.HARD_CUT_LEVEL
    TuningSlot(stringResource(R.string.audio_hard_cut_level, "%.1f".format(tuning[cut])), tuning, cut, onChange)
    val halfLife = SoundTuning.HARD_CUT_HALF_LIFE
    TuningSlot(stringResource(R.string.audio_hard_cut_half_life, tuning[halfLife].roundToInt()), tuning, halfLife, onChange)
    val pitch = SoundTuning.PITCH_THRESHOLD
    TuningSlot(stringResource(R.string.audio_pitch_threshold, "%.2f".format(tuning[pitch])), tuning, pitch, onChange)
    val hold = SoundTuning.NOTE_HOLD
    TuningSlot(stringResource(R.string.audio_note_hold, ms(tuning[hold])), tuning, hold, onChange)
    val silence = SoundTuning.SILENCE_DB
    TuningSlot(stringResource(R.string.audio_silence, tuning[silence].roundToInt()), tuning, silence, onChange)
    val trim = SoundTuning.INPUT_TRIM_DB
    TuningSlot(stringResource(R.string.audio_input_trim, "%+.0f".format(tuning[trim])), tuning, trim, onChange)
}

/** One tuning slot's slider, over the range native gives that slot. */
@Composable
private fun TuningSlot(
    label: String,
    tuning: SoundTuning,
    slot: Int,
    onChange: (SoundTuning) -> Unit,
) = TuningSlider(label, tuning[slot], SoundTuning.range(slot)) { onChange(tuning.withSlot(slot, it)) }

@Composable
private fun TuningSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
) {
    Text(label, style = MaterialTheme.typography.labelMedium)
    CrystalSlider(value = value, onValueChange = onChange, valueRange = range)
}
