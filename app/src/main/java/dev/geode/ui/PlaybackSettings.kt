package dev.geode.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.analysis.PlaybackMath
import dev.geode.data.PlayerPrefs
import dev.geode.playback.BitPerfectOutput
import dev.geode.playback.ReplayGain

private val SLEEP_TIMER_CHOICES = listOf(0, 15, 30, 45, 60)

private val REPLAYGAIN_MODES =
    listOf(
        ReplayGain.MODE_OFF to R.string.playback_replaygain_off,
        ReplayGain.MODE_TRACK to R.string.playback_replaygain_track,
        ReplayGain.MODE_ALBUM to R.string.playback_replaygain_album,
    )

/**
 * Settings › Playback › Basics: what people reach for first in a music player's playback
 * settings (docs/research/settings-behaviour.md): the crossfade between tracks, the fade on play
 * and pause, pausing when headphones come out, resuming on launch and the sleep timer.
 */
@Composable
internal fun PlaybackBasics(viewModel: SettingsViewModel) {
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    val sleepRemainingMs by playerViewModel.sleepTimerRemainingMs.collectAsStateWithLifecycle()
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        CrossfadeSlider(prefs) { viewModel.setPlayerPrefs(it) }
        Column {
            CrystalSlider(
                label =
                    if (prefs.fadeMs <= 0) {
                        stringResource(R.string.playback_fade_off)
                    } else {
                        stringResource(R.string.playback_fade, "%.1f".format(prefs.fadeMs / 1000f))
                    },
                value = prefs.fadeMs.toFloat(),
                onValueChange = {
                    viewModel.setPlayerPrefs(prefs.copy(fadeMs = (PlaybackMath.snap(it, 250f)).toInt()))
                },
                valueRange = 0f..PlayerPrefs.MAX_FADE_MS.toFloat(),
                accessibilityStep = 250f,
            )
            Text(
                stringResource(R.string.playback_fade_explainer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        PlaybackSwitchRow(stringResource(R.string.playback_pause_unplugged), prefs.pauseOnNoisy) {
            viewModel.setPlayerPrefs(prefs.copy(pauseOnNoisy = it))
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

/**
 * Crossfade (#338): Off, then 1–12 s in whole seconds, off by default. Written to
 * [PlayerPrefs.crossfadeMs], which `playback/TrackCrossfade` reads at the next track change, so a
 * change applies to the next join with no restart.
 */
@Composable
private fun CrossfadeSlider(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    val seconds = prefs.crossfadeMs / PlayerPrefs.CROSSFADE_STEP_MS
    val maxSeconds = PlayerPrefs.MAX_CROSSFADE_MS / PlayerPrefs.CROSSFADE_STEP_MS
    Column {
        CrystalSlider(
            label =
                if (seconds <= 0) {
                    stringResource(R.string.playback_crossfade_off)
                } else {
                    pluralStringResource(R.plurals.playback_crossfade_seconds, seconds, seconds)
                },
            value = seconds.toFloat(),
            onValueChange = { onChange(prefs.copy(crossfadeMs = PlayerPrefs.crossfadeMsFromSeconds(it))) },
            valueRange = 0f..maxSeconds.toFloat(),
            // One stop per whole second between Off and the longest.
            steps = maxSeconds - 1,
        )
        Text(
            stringResource(R.string.playback_crossfade_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** Settings › Playback › Speed & pitch. */
@Composable
internal fun SpeedPitchSection(viewModel: SettingsViewModel) {
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    Column {
        CrystalSlider(
            label = stringResource(R.string.playback_speed, "%.2f".format(prefs.speed)),
            value = prefs.speed,
            onValueChange = { viewModel.setPlayerPrefs(prefs.copy(speed = PlaybackMath.snap(it, 0.05f))) },
            valueRange = 0.5f..2f,
            accessibilityStep = 0.05f,
        )
    }
    Column {
        CrystalSlider(
            label = stringResource(R.string.playback_pitch, "%.1f".format(prefs.pitchSemitones)),
            value = prefs.pitchSemitones,
            onValueChange = { viewModel.setPlayerPrefs(prefs.copy(pitchSemitones = PlaybackMath.snap(it, 0.5f))) },
            valueRange = -6f..6f,
            accessibilityStep = 0.5f,
        )
    }
    Text(
        stringResource(R.string.audio_dsp_visuals_notice),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** Settings › Playback › Volume levelling: ReplayGain. */
@Composable
internal fun VolumeLevellingSection(viewModel: SettingsViewModel) {
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    ReplayGainSettings(prefs) { viewModel.setPlayerPrefs(it) }
}

/** Settings › Playback › Output and more: bit-perfect USB, the native output, skipping silence, Player's screen, long tracks. */
@Composable
internal fun PlaybackMoreSection(viewModel: SettingsViewModel) {
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    BitPerfectSettings(prefs) { viewModel.setPlayerPrefs(it) }
    NativeOutputSettings(prefs) { viewModel.setPlayerPrefs(it) }
    PlaybackSwitchRow(stringResource(R.string.playback_skip_silence), prefs.skipSilence) {
        viewModel.setPlayerPrefs(prefs.copy(skipSilence = it))
    }
    PlaybackSwitchRow(stringResource(R.string.playback_keep_screen_on), prefs.keepScreenOn) {
        viewModel.setPlayerPrefs(prefs.copy(keepScreenOn = it))
    }
    PlaybackSwitchRow(stringResource(R.string.playback_resume_long_tracks), prefs.resumeLongTracks) {
        viewModel.setPlayerPrefs(prefs.copy(resumeLongTracks = it))
    }
    Text(
        stringResource(R.string.playback_resume_long_tracks_explainer),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
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
            CrystalSlider(
                label = stringResource(R.string.playback_replaygain_preamp, "%.1f".format(prefs.replayGainPreampDb)),
                value = prefs.replayGainPreampDb,
                onValueChange = { onChange(prefs.copy(replayGainPreampDb = PlaybackMath.snap(it, 0.5f))) },
                valueRange = -PlayerPrefs.MAX_REPLAYGAIN_PREAMP_DB..PlayerPrefs.MAX_REPLAYGAIN_PREAMP_DB,
                accessibilityStep = 0.5f,
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

/** Shown only where Android can open a USB DAC's port bit-perfect (API 34+). */
@Composable
private fun BitPerfectSettings(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    if (!BitPerfectOutput.supported) return
    Column {
        PlaybackSwitchRow(stringResource(R.string.playback_bit_perfect), prefs.bitPerfect) {
            onChange(prefs.copy(bitPerfect = it))
        }
        Text(
            stringResource(R.string.playback_bit_perfect_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        // Said before it is switched on too, so nobody finds their EQ or crossfade gone and calls it a bug.
        Text(
            stringResource(R.string.playback_bit_perfect_paused),
            style = MaterialTheme.typography.bodySmall,
            color = if (prefs.bitPerfect) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** The Oboe output under ExoPlayer (`audio/OboeAudioOutput`); what it cannot play, the platform track plays. */
@Composable
private fun NativeOutputSettings(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        PlaybackSwitchRow(stringResource(R.string.playback_native_output), prefs.nativeOutput) {
            onChange(prefs.copy(nativeOutput = it))
        }
        Text(
            stringResource(R.string.playback_native_output_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun PlaybackSwitchRow(
    label: String,
    checked: Boolean,
    onChange: (Boolean) -> Unit,
) {
    LabeledSwitch(
        label,
        checked = checked,
        onCheckedChange = onChange,
    )
}
