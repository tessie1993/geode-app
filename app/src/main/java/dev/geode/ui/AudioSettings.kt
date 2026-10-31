package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
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

@Composable
internal fun AudioSettingsTab(viewModel: PlayerViewModel) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.audio_group_playback)) { PlaybackSettingsSection(settingsViewModel) } }
        item { EqualizerSettings(settingsViewModel) }
        item { SettingsGroup(stringResource(R.string.audio_group_analysis)) { AnalysisGroup(settingsViewModel) } }
        item { SettingsGroup(stringResource(R.string.source_live_input)) { LiveInputGroup(viewModel) } }
        item { SettingsGroup(stringResource(R.string.source_other_apps)) { ExternalAudioSettings(viewModel) } }
    }
}

@Composable
private fun AnalysisGroup(viewModel: SettingsViewModel) {
    val playerViewModel: PlayerViewModel = geodeViewModel()
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
    Column {
        LabeledSwitch(
            stringResource(R.string.audio_key_colour),
            checked = gui.keyColor,
            onCheckedChange = playerViewModel::setKeyColor,
        )
        Text(
            stringResource(R.string.audio_key_colour_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
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
    Column {
        Text(stringResource(R.string.audio_tune_room), style = MaterialTheme.typography.labelMedium)
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(
                dev.geode.analysis.LiveInputProfile.entries
                    .toList(),
            ) { profile ->
                CrystalButton(
                    compact = true,
                    filled = false,
                    onClick = { viewModel.applyLiveInputProfile(profile) },
                ) { Text(profile.label) }
            }
        }
        Text(
            dev.geode.analysis.LiveInputProfile.entries
                .joinToString("  ·  ") { "${it.label}: ${it.summary}" },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            stringResource(R.string.audio_profiles_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
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
        val reactivity = stringResource(R.string.audio_reactivity, "%.2f".format(tuning.reactivity))
        TuningSlider(reactivity, tuning.reactivity, SoundTuning.REACTIVITY) { onChange(tuning.copy(reactivity = it)) }
        TuningSlider(stringResource(R.string.audio_speed, "%.2f".format(tuning.speed)), tuning.speed, SoundTuning.SPEED) {
            onChange(tuning.copy(speed = it))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(filled = false, onClick = { advanced = !advanced }) { Text(stringResource(R.string.audio_advanced)) }
            TextButton(onClick = { onChange(SoundTuning()) }) { Text(stringResource(R.string.audio_default)) }
        }
        if (advanced) AdvancedTuning(tuning, onChange)
    }
}

@Composable
private fun AdvancedTuning(
    tuning: SoundTuning,
    onChange: (SoundTuning) -> Unit,
) {
    fun List<Float>.with(
        i: Int,
        v: Float,
    ) = toMutableList().also { it[i] = v }
    val bands = listOf(R.string.audio_band_low, R.string.audio_band_middle, R.string.audio_band_high)
    bands.forEachIndexed { i, name ->
        Text(stringResource(name), style = MaterialTheme.typography.titleSmall)
        TuningSlider(stringResource(R.string.audio_band_gain, "%.2f".format(tuning.gain[i])), tuning.gain[i], SoundTuning.GAIN) {
            onChange(tuning.copy(gain = tuning.gain.with(i, it)))
        }
        TuningSlider(stringResource(R.string.audio_band_threshold, "%.2f".format(tuning.threshold[i])), tuning.threshold[i], 0f..1f) {
            onChange(tuning.copy(threshold = tuning.threshold.with(i, it)))
        }
        TuningSlider(stringResource(R.string.audio_band_attack, (tuning.attack[i] * 1000).roundToInt()), tuning.attack[i], 0f..1f) {
            onChange(tuning.copy(attack = tuning.attack.with(i, it)))
        }
        TuningSlider(stringResource(R.string.audio_band_release, (tuning.release[i] * 1000).roundToInt()), tuning.release[i], 0f..2f) {
            onChange(tuning.copy(release = tuning.release.with(i, it)))
        }
    }
    Text(stringResource(R.string.audio_drums), style = MaterialTheme.typography.titleSmall)
    listOf(R.string.audio_drum_kick, R.string.audio_drum_snare, R.string.audio_drum_hat).forEachIndexed { i, label ->
        TuningSlider(
            stringResource(label, "%.2f".format(tuning.drum[i]), (tuning.drumDecay[i] * 1000).roundToInt()),
            tuning.drum[i],
            SoundTuning.DRUM,
        ) { onChange(tuning.copy(drum = tuning.drum.with(i, it))) }
        CrystalSlider(
            value = tuning.drumDecay[i],
            onValueChange = { onChange(tuning.copy(drumDecay = tuning.drumDecay.with(i, it))) },
            valueRange = 0.02f..1f,
        )
    }
    Text(stringResource(R.string.audio_events), style = MaterialTheme.typography.titleSmall)
    TuningSlider(stringResource(R.string.audio_onset_threshold, "%.1f".format(tuning.onsetThreshold)), tuning.onsetThreshold, 1f..4f) {
        onChange(tuning.copy(onsetThreshold = it))
    }
    TuningSlider(stringResource(R.string.audio_hard_cut_level, "%.1f".format(tuning.hardCutLevel)), tuning.hardCutLevel, 1.5f..4f) {
        onChange(tuning.copy(hardCutLevel = it))
    }
    TuningSlider(stringResource(R.string.audio_hard_cut_half_life, tuning.hardCutHalfLife.roundToInt()), tuning.hardCutHalfLife, 5f..180f) {
        onChange(tuning.copy(hardCutHalfLife = it))
    }
    TuningSlider(stringResource(R.string.audio_pitch_threshold, "%.2f".format(tuning.pitchThreshold)), tuning.pitchThreshold, 0.1f..0.9f) {
        onChange(tuning.copy(pitchThreshold = it))
    }
    TuningSlider(stringResource(R.string.audio_note_hold, (tuning.noteHold * 1000).roundToInt()), tuning.noteHold, 0.02f..0.5f) {
        onChange(tuning.copy(noteHold = it))
    }
    TuningSlider(stringResource(R.string.audio_silence, tuning.silenceDb.roundToInt()), tuning.silenceDb, -90f..-30f) {
        onChange(tuning.copy(silenceDb = it))
    }
}

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
