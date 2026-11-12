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
import dev.geode.data.PlayerPrefs
import dev.geode.engine.bridge.GeodeNative
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
 * Every Settings row stored in [PlayerPrefs], whichever tab draws it: Playback's crossfade, fades,
 * resume, sleep timer, speed and pitch, ReplayGain, skipping silence and long tracks; Output &
 * devices' pause when unplugged, the native output and bit-perfect USB; Displays & casting's keep
 * the screen on while the visual shows. One write path ([SettingsViewModel.setPlayerPrefs]), so a
 * row moved between tabs stores exactly what it did.
 */
@Composable
internal fun PlayerPrefsRow(
    row: SettingsRowKey,
    viewModel: SettingsViewModel,
) {
    val prefs by viewModel.playerPrefs.collectAsStateWithLifecycle()
    val set: (PlayerPrefs) -> Unit = { viewModel.setPlayerPrefs(it) }
    when (row) {
        SettingsRowKey.CROSSFADE -> CrossfadeSlider(prefs, set)
        SettingsRowKey.FADE -> FadeSlider(prefs, set)
        SettingsRowKey.AUTO_RESUME ->
            PlaybackSwitchRow(stringResource(R.string.playback_auto_resume), prefs.autoResume) { set(prefs.copy(autoResume = it)) }
        SettingsRowKey.SLEEP_TIMER -> SleepTimerRow(prefs, set)
        SettingsRowKey.SPEED_PITCH -> SpeedPitch(prefs, set)
        SettingsRowKey.REPLAYGAIN -> ReplayGainSettings(prefs, set)
        SettingsRowKey.SKIP_SILENCE ->
            PlaybackSwitchRow(stringResource(R.string.playback_skip_silence), prefs.skipSilence) { set(prefs.copy(skipSilence = it)) }
        SettingsRowKey.RESUME_LONG_TRACKS -> ResumeLongTracks(prefs, set)
        SettingsRowKey.PAUSE_UNPLUGGED ->
            PlaybackSwitchRow(stringResource(R.string.playback_pause_unplugged), prefs.pauseOnNoisy) { set(prefs.copy(pauseOnNoisy = it)) }
        SettingsRowKey.NATIVE_OUTPUT -> NativeOutputSettings(prefs, set)
        SettingsRowKey.BIT_PERFECT -> BitPerfectSettings(prefs, set)
        SettingsRowKey.PICTURE_DELAY -> PictureDelaySlider(prefs, set)
        SettingsRowKey.KEEP_SCREEN_ON_WITH_VISUAL ->
            PlaybackSwitchRow(stringResource(R.string.playback_keep_screen_on), prefs.keepScreenOn) { set(prefs.copy(keepScreenOn = it)) }
        else -> Unit
    }
}

/** The visual's delay behind the sound, for a headset that misreports its latency; the bound is native. */
@Composable
private fun PictureDelaySlider(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    val max = GeodeNative.playoutMaxOffsetMs().toFloat()
    Column {
        CrystalSlider(
            label = stringResource(R.string.playback_picture_delay, prefs.pictureDelayMs),
            value = prefs.pictureDelayMs.toFloat(),
            onValueChange = { onChange(prefs.copy(pictureDelayMs = GeodeNative.playbackSnap(it, PICTURE_DELAY_STEP_MS).toInt())) },
            valueRange = -max..max,
            accessibilityStep = PICTURE_DELAY_STEP_MS,
        )
        Text(
            stringResource(R.string.playback_picture_delay_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private const val PICTURE_DELAY_STEP_MS = 10f

/** The fade on play and pause, in quarter seconds. */
@Composable
private fun FadeSlider(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        CrystalSlider(
            label =
                if (prefs.fadeMs <= 0) {
                    stringResource(R.string.playback_fade_off)
                } else {
                    stringResource(R.string.playback_fade, "%.1f".format(prefs.fadeMs / 1000f))
                },
            value = prefs.fadeMs.toFloat(),
            onValueChange = { onChange(prefs.copy(fadeMs = (GeodeNative.playbackSnap(it, 250f)).toInt())) },
            valueRange = 0f..PlayerPrefs.MAX_FADE_MS.toFloat(),
            accessibilityStep = 250f,
        )
        Text(
            stringResource(R.string.playback_fade_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** The sleep timer's time left as m:ss, a started second counting as a whole one so it never shows 0:00 early. */
private fun formatCountdown(remainingMs: Long): String {
    val totalSec = (remainingMs.coerceAtLeast(0L) + 999L) / 1000L
    return "%d:%02d".format(totalSec / 60, totalSec % 60)
}

/** The sleep timer's choices, finishing the track first, and the countdown while it runs. */
@Composable
private fun SleepTimerRow(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val sleepRemainingMs by playerViewModel.sleepTimerRemainingMs.collectAsStateWithLifecycle()
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
            onChange(prefs.copy(sleepFinishTrack = it))
        }
        sleepRemainingMs?.let { remaining ->
            Text(
                stringResource(
                    if (prefs.sleepFinishTrack) {
                        R.string.playback_sleep_pausing_after_track
                    } else {
                        R.string.playback_sleep_pausing_in
                    },
                    formatCountdown(remaining),
                ),
                style = MaterialTheme.typography.bodySmall,
                color = accentTextColor(),
            )
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

/** Playback › Speed & pitch, with the note that the visuals hear the music before either. */
@Composable
private fun SpeedPitch(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        CrystalSlider(
            label = stringResource(R.string.playback_speed, "%.2f".format(prefs.speed)),
            value = prefs.speed,
            onValueChange = { onChange(prefs.copy(speed = GeodeNative.playbackSnap(it, 0.05f))) },
            valueRange = 0.5f..2f,
            accessibilityStep = 0.05f,
        )
    }
    Column {
        CrystalSlider(
            label = stringResource(R.string.playback_pitch, "%.1f".format(prefs.pitchSemitones)),
            value = prefs.pitchSemitones,
            onValueChange = { onChange(prefs.copy(pitchSemitones = GeodeNative.playbackSnap(it, 0.5f))) },
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

/** Playback › More: where a long track (an audiobook, a mix) picks up when it is played again. */
@Composable
private fun ResumeLongTracks(
    prefs: PlayerPrefs,
    onChange: (PlayerPrefs) -> Unit,
) {
    Column {
        PlaybackSwitchRow(stringResource(R.string.playback_resume_long_tracks), prefs.resumeLongTracks) {
            onChange(prefs.copy(resumeLongTracks = it))
        }
        Text(
            stringResource(R.string.playback_resume_long_tracks_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
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
            CrystalSlider(
                label = stringResource(R.string.playback_replaygain_preamp, "%.1f".format(prefs.replayGainPreampDb)),
                value = prefs.replayGainPreampDb,
                onValueChange = { onChange(prefs.copy(replayGainPreampDb = GeodeNative.playbackSnap(it, 0.5f))) },
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
