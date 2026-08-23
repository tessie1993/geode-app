package dev.geode.ui

import android.app.Activity
import android.media.projection.MediaProjectionManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.CaptureFailure
import dev.geode.audio.PlaybackCaptureService

@Composable
fun ExternalAudioSettings(viewModel: PlayerViewModel) {
    val context = LocalContext.current
    val external by viewModel.externalAudio.collectAsStateWithLifecycle()

    val projectionLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val data = result.data
            if (result.resultCode == Activity.RESULT_OK && data != null) {
                PlaybackCaptureService.start(context, result.resultCode, data)
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }
    val askConsent = {
        viewModel.noteExternalAudioConsentPending()
        val manager = context.getSystemService(MediaProjectionManager::class.java)
        projectionLauncher.launch(manager.createScreenCaptureIntent())
    }
    val permissions =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
            if (granted[android.Manifest.permission.RECORD_AUDIO] != false) {
                askConsent()
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }

    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (!external.supported) {
            Text(stringResource(R.string.ext_visualize_other_apps), style = MaterialTheme.typography.bodyMedium)
            Text(
                stringResource(R.string.ext_needs_android10),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            return@Column
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.ext_visualize_other_apps),
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
            )
            Switch(
                checked = external.active || external.awaitingConsent,
                onCheckedChange = { want ->
                    if (!want) {
                        viewModel.stopExternalAudio()
                    } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        permissions.launch(
                            arrayOf(
                                android.Manifest.permission.RECORD_AUDIO,
                                android.Manifest.permission.POST_NOTIFICATIONS,
                            ),
                        )
                    } else if (viewModel.hasMicPermission()) {
                        askConsent()
                    } else {
                        permissions.launch(arrayOf(android.Manifest.permission.RECORD_AUDIO))
                    }
                },
            )
        }
        Text(
            stringResource(R.string.ext_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        when {
            external.awaitingConsent ->
                Text(
                    stringResource(R.string.ext_waiting_permission),
                    style = MaterialTheme.typography.bodySmall,
                    color = accentTextColor(),
                )
            external.refusedByApp -> RefusedNotice(viewModel, external)
            external.active ->
                Text(
                    external.nowPlaying?.let { stringResource(R.string.ext_listening_to, it.appLabel) }
                        ?: stringResource(R.string.ext_listening_idle),
                    style = MaterialTheme.typography.bodySmall,
                    color = accentTextColor(),
                )
            else ->
                external.failure?.let { failure ->
                    Text(stringResource(failureText(failure)), style = MaterialTheme.typography.bodySmall)
                }
        }

        external.nowPlaying?.takeIf { it.title.isNotBlank() }?.let { np ->
            Text(
                if (np.artist.isNotBlank()) {
                    stringResource(R.string.ext_now_playing_line_with_artist, np.appLabel, np.title, np.artist)
                } else {
                    stringResource(R.string.ext_now_playing_line, np.appLabel, np.title)
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        if (!external.hasSessionAccess) {
            Column {
                Text(
                    stringResource(R.string.ext_read_now_playing_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                CrystalButton(
                    filled = false,
                    modifier = Modifier.padding(top = 6.dp),
                    onClick = {
                        runCatching { context.startActivity(viewModel.notificationAccessIntent()) }
                    },
                ) { Text(stringResource(R.string.ext_allow_reading)) }
            }
        }
    }
}

@Composable
private fun RefusedNotice(
    viewModel: PlayerViewModel,
    external: ExternalAudioState,
) {
    val app = external.refusingApp ?: stringResource(R.string.subtitle_capture_refused_unknown_app)
    Column(
        Modifier
            .fillMaxWidth()
            .crystalPanel(
                0.32f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.error,
                corner = 16.dp,
                glowStrength = 0.5f,
            ).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        CrystalOverline(stringResource(R.string.ext_silence_title), color = MaterialTheme.colorScheme.error)
        Text(
            stringResource(R.string.ext_refused_body, app),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            stringResource(R.string.ext_mic_alternative, app),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        CrystalButton(onClick = {
            viewModel.stopExternalAudio()
            viewModel.setMicEnabled(true)
        }) { Text(stringResource(R.string.ext_use_mic_instead)) }
    }
}

private fun failureText(failure: CaptureFailure): Int =
    when (failure) {
        CaptureFailure.UNSUPPORTED -> R.string.ext_fail_unsupported
        CaptureFailure.PERMISSION -> R.string.ext_fail_permission
        CaptureFailure.CONSENT -> R.string.ext_fail_consent
        CaptureFailure.UNAVAILABLE -> R.string.ext_fail_unavailable
    }
