package dev.geode.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.analysis.PlaybackMath
import dev.geode.data.PlayerPrefs
import dev.geode.playback.ReplayGain

private val SLEEP_TIMER_CHOICES = listOf(0, 15, 30, 45, 60)

private val CROSSFADE_CURVES =
    listOf(
        0 to R.string.playback_crossfade_linear,
        1 to R.string.playback_crossfade_equal_power,
        2 to R.string.playback_crossfade_smooth,
    )

private val REPLAYGAIN_MODES =
    listOf(
        ReplayGain.MODE_OFF to R.string.playback_replaygain_off,
        ReplayGain.MODE_TRACK to R.string.playback_replaygain_track,
        ReplayGain.MODE_ALBUM to R.string.playback_replaygain_album,
    )

@Composable
fun PlaybackSettingsSection(viewModel: SettingsViewModel) {
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    val sleepRemainingMs by playerViewModel.sleepTimerRemainingMs.collectAsStateWithLifecycle()
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Column {
            Text(
                stringResource(R.string.playback_speed, "%.2f".format(prefs.speed)),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = prefs.speed,
                onValueChange = { viewModel.setPlayerPrefs(prefs.copy(speed = PlaybackMath.snap(it, 0.05f))) },
                valueRange = 0.5f..2f,
            )
        }
        Column {
            Text(
                stringResource(R.string.playback_pitch, "%.1f".format(prefs.pitchSemitones)),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = prefs.pitchSemitones,
                onValueChange = { viewModel.setPlayerPrefs(prefs.copy(pitchSemitones = PlaybackMath.snap(it, 0.5f))) },
                valueRange = -6f..6f,
            )
        }
        Column {
            Text(
                if (prefs.fadeMs <= 0) {
                    stringResource(R.string.playback_fade_off)
                } else {
                    stringResource(R.string.playback_fade, "%.1f".format(prefs.fadeMs / 1000f))
                },
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = prefs.fadeMs.toFloat(),
                onValueChange = {
                    viewModel.setPlayerPrefs(prefs.copy(fadeMs = (PlaybackMath.snap(it, 250f)).toInt()))
                },
                valueRange = 0f..PlayerPrefs.MAX_FADE_MS.toFloat(),
            )
            Text(
                stringResource(R.string.playback_fade_explainer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        ReplayGainSettings(prefs) { viewModel.setPlayerPrefs(it) }
        NativeEngineSettings(prefs) { viewModel.setPlayerPrefs(it) }
        PlaybackSwitchRow(stringResource(R.string.playback_skip_silence), prefs.skipSilence) {
            viewModel.setPlayerPrefs(prefs.copy(skipSilence = it))
        }
        PlaybackSwitchRow(stringResource(R.string.playback_pause_unplugged), prefs.pauseOnNoisy) {
            viewModel.setPlayerPrefs(prefs.copy(pauseOnNoisy = it))
        }
        PlaybackSwitchRow(stringResource(R.string.playback_keep_screen_on), prefs.keepScreenOn) {
            viewModel.setPlayerPrefs(prefs.copy(keepScreenOn = it))
        }
        PlaybackSwitchRow(stringResource(R.string.playback_auto_resume), prefs.autoResume) {
            viewModel.setPlayerPrefs(prefs.copy(autoResume = it))
        }
        Column {
            Text(stringResource(R.string.playback_sleep_timer), style = MaterialTheme.typography.labelMedium)
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                val running = sleepRemainingMs != null
                SLEEP_TIMER_CHOICES.forEach { minutes ->
                    FilterChip(
                        selected =
                            if (running) {
                                minutes != 0 && minutes == prefs.sleepTimerMinutes
                            } else {
                                minutes == 0
                            },
                        onClick = {
                            if (minutes == 0) playerViewModel.cancelSleepTimer() else playerViewModel.startSleepTimer(minutes)
                        },
                        label = {
                            Text(
                                if (minutes == 0) {
                                    stringResource(R.string.playback_sleep_off)
                                } else {
                                    stringResource(R.string.playback_sleep_minutes, minutes)
                                },
                                style = MaterialTheme.typography.labelSmall,
                            )
                        },
                    )
                }
            }
            PlaybackSwitchRow(stringResource(R.string.playback_sleep_finish_track), prefs.sleepFinishTrack) {
                viewModel.setPlayerPrefs(prefs.copy(sleepFinishTrack = it))
            }
            sleepRemainingMs?.let { remaining ->
                Text(
                    stringResource(
                        if (prefs.sleepFinishTrack) {
                            R.string.playback_sleep_pausing_after_track
                        } else {
                            R.string.playback_sleep_pausing_in
                        },
                        PlaybackMath.formatCountdown(remaining),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = accentTextColor(),
                )
            }
        }
    }
}

@Composable
private fun ReplayGainSettings(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        Text(stringResource(R.string.playback_replaygain), style = MaterialTheme.typography.labelMedium)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            REPLAYGAIN_MODES.forEach { (mode, label) ->
                FilterChip(
                    selected = prefs.replayGainMode == mode,
                    onClick = { onChange(prefs.copy(replayGainMode = mode)) },
                    label = { Text(stringResource(label), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        if (prefs.replayGainMode != ReplayGain.MODE_OFF) {
            Text(
                stringResource(R.string.playback_replaygain_preamp, "%.1f".format(prefs.replayGainPreampDb)),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = prefs.replayGainPreampDb,
                onValueChange = { onChange(prefs.copy(replayGainPreampDb = PlaybackMath.snap(it, 0.5f))) },
                valueRange = -PlayerPrefs.MAX_REPLAYGAIN_PREAMP_DB..PlayerPrefs.MAX_REPLAYGAIN_PREAMP_DB,
            )
            PlaybackSwitchRow(stringResource(R.string.playback_replaygain_clip_guard), prefs.replayGainClipGuard) {
                onChange(prefs.copy(replayGainClipGuard = it))
            }
        }
        Text(
            stringResource(R.string.playback_replaygain_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun NativeEngineSettings(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        PlaybackSwitchRow(stringResource(R.string.playback_native_engine), prefs.nativeEngine) {
            onChange(prefs.copy(nativeEngine = it))
        }
        Text(
            stringResource(R.string.playback_native_engine_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (!prefs.nativeEngine) return
        PlaybackSwitchRow(stringResource(R.string.playback_gapless), prefs.gapless) {
            onChange(prefs.copy(gapless = it))
        }
        Text(
            if (prefs.crossfadeMs <= 0) {
                stringResource(R.string.playback_crossfade_off)
            } else {
                stringResource(R.string.playback_crossfade, "%.1f".format(prefs.crossfadeMs / 1000f))
            },
            style = MaterialTheme.typography.labelMedium,
        )
        CrystalSlider(
            value = prefs.crossfadeMs.toFloat(),
            onValueChange = { onChange(prefs.copy(crossfadeMs = PlaybackMath.snap(it, 500f).toInt())) },
            valueRange = 0f..PlayerPrefs.MAX_CROSSFADE_MS.toFloat(),
        )
        if (prefs.crossfadeMs > 0) {
            Text(stringResource(R.string.playback_crossfade_curve), style = MaterialTheme.typography.labelMedium)
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                CROSSFADE_CURVES.forEach { (curve, label) ->
                    FilterChip(
                        selected = prefs.crossfadeCurve == curve,
                        onClick = { onChange(prefs.copy(crossfadeCurve = curve)) },
                        label = { Text(stringResource(label), style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
        }
    }
}

@Composable
private fun PlaybackSwitchRow(
    label: String,
    checked: Boolean,
    onChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}
