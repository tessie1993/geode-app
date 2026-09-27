package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.analysis.BeatTuning
import dev.geode.ui.opaline.creative.CreativeButton
import dev.geode.ui.opaline.creative.CreativeSlider
import dev.geode.ui.opaline.creative.CreativeToggle
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
        Text(
            stringResource(R.string.audio_beat_sensitivity, "%.1f".format(gui.beatSensitivity)),
            style = MaterialTheme.typography.labelMedium,
        )
        CreativeSlider(
            value = gui.beatSensitivity,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(beatSensitivity = it)) },
            valueRange = BeatTuning.SENSITIVITY_MIN..BeatTuning.SENSITIVITY_MAX,
        )
        Text(
            stringResource(
                R.string.audio_beat_min_gap,
                gui.beatMinIntervalMs.roundToInt(),
                (60_000f / gui.beatMinIntervalMs).roundToInt(),
            ),
            style = MaterialTheme.typography.labelMedium,
        )
        CreativeSlider(
            value = gui.beatMinIntervalMs,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(beatMinIntervalMs = it)) },
            valueRange = BeatTuning.INTERVAL_MS_MIN..BeatTuning.INTERVAL_MS_MAX,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CreativeButton(
                text = stringResource(R.string.audio_slow_track),
                onClick = {
                    viewModel.setGuiPrefs(
                        gui.copy(
                            beatSensitivity = BeatTuning.SLOW_SENSITIVITY,
                            beatMinIntervalMs = BeatTuning.SLOW_INTERVAL_MS,
                        ),
                    )
                },
            )
            CreativeButton(
                text = stringResource(R.string.audio_default),
                onClick = {
                    viewModel.setGuiPrefs(
                        gui.copy(
                            beatSensitivity = BeatTuning.SENSITIVITY_DEFAULT,
                            beatMinIntervalMs = BeatTuning.INTERVAL_MS_DEFAULT,
                        ),
                    )
                },
            )
        }
    }
    Column {
        Text(stringResource(R.string.audio_preset_morph, gui.presetMorphSeconds))
        CreativeSlider(
            value = gui.presetMorphSeconds,
            onValueChange = { viewModel.setGuiPrefs(gui.copy(presetMorphSeconds = it)) },
            valueRange = 0f..ThemeStore.PRESET_MORPH_SECONDS_MAX,
        )
    }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.audio_key_colour),
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
            )
            CreativeToggle(checked = gui.keyColor, onCheckedChange = playerViewModel::setKeyColor)
        }
        Text(
            stringResource(R.string.audio_key_colour_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
internal fun LiveInputGroup(viewModel: PlayerViewModel) {
    val mic by viewModel.micState.collectAsStateWithLifecycle()
    var denied by rememberSaveable { mutableStateOf(false) }
    val micPermission =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            denied = !granted
            if (granted) viewModel.setMicEnabled(true)
        }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.audio_react_mic),
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
            )
            CreativeToggle(
                checked = mic.active,
                onCheckedChange = { want ->
                    denied = false
                    if (!want) {
                        viewModel.setMicEnabled(false)
                    } else if (viewModel.hasMicPermission()) {
                        viewModel.setMicEnabled(true)
                    } else {
                        micPermission.launch(android.Manifest.permission.RECORD_AUDIO)
                    }
                },
            )
        }
        Text(
            stringResource(R.string.audio_react_mic_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        when {
            denied || mic.failure == dev.geode.audio.MicCapture.Failure.PERMISSION ->
                Text(
                    stringResource(R.string.audio_mic_denied),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                )
            mic.failure == dev.geode.audio.MicCapture.Failure.UNAVAILABLE ->
                Text(
                    stringResource(R.string.audio_mic_busy),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                )
        }
    }
    Column {
        Text(stringResource(R.string.audio_tune_room), style = MaterialTheme.typography.labelMedium)
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(
                dev.geode.analysis.LiveInputProfile.entries
                    .toList(),
            ) { profile ->
                CreativeButton(
                    text = profile.label,
                    onClick = { viewModel.applyLiveInputProfile(profile) },
                )
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
