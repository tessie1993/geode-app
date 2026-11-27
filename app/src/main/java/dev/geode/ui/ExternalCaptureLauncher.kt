package dev.geode.ui

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.geode.R
import dev.geode.audio.PlaybackCaptureService

/** Starts listening to other apps: the disclosure, the microphone permission, then Android's capture consent. */
fun interface ExternalCaptureLauncher {
    fun start()
}

/** Per process: a restarted app discloses again before its first capture. */
internal object CaptureDisclosure {
    var accepted = false
    var notificationsAsked = false
}

/**
 * The one way the app starts a capture of other apps' audio (Settings, the Player's quick actions, the
 * empty library). Play wants the app's own disclosure before the permission it leads to, so it shows
 * whenever the microphone is not granted, and once per process otherwise. The notification
 * permission is asked only after the capture has started. [onStarted] runs once Android consented.
 */
@Composable
fun rememberExternalCaptureLauncher(
    viewModel: PlayerViewModel,
    onStarted: () -> Unit = {},
): ExternalCaptureLauncher {
    val context = LocalContext.current
    val currentOnStarted by rememberUpdatedState(onStarted)
    val notifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val projection =
        rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val data = result.data
            if (result.resultCode != Activity.RESULT_OK || data == null) {
                viewModel.noteExternalAudioConsentDenied()
            } else if (!PlaybackCaptureService.start(context, result.resultCode, data)) {
                viewModel.noteExternalAudioStartFailed()
            } else {
                if (!CaptureDisclosure.notificationsAsked && !context.granted(Manifest.permission.POST_NOTIFICATIONS)) {
                    CaptureDisclosure.notificationsAsked = true
                    notifications.launch(Manifest.permission.POST_NOTIFICATIONS)
                }
                currentOnStarted()
            }
        }
    return rememberExternalCaptureLauncher(
        askConsent = {
            viewModel.noteExternalAudioConsentPending()
            projection.launch(context.screenCaptureIntent())
        },
        onMicRefused = viewModel::noteExternalAudioPermissionDenied,
        micRequest = { onGranted, onRefused ->
            rememberPermissionRequest(Manifest.permission.RECORD_AUDIO, onRefused = onRefused, onGranted = onGranted)
        },
    )
}

/** The flow with the system's dialogs passed in: [askConsent] shows Android's capture consent. */
@Composable
internal fun rememberExternalCaptureLauncher(
    askConsent: () -> Unit,
    onMicRefused: () -> Unit,
    micRequest: @Composable (onGranted: () -> Unit, onRefused: () -> Unit) -> PermissionRequestState,
): ExternalCaptureLauncher {
    val currentAskConsent by rememberUpdatedState(askConsent)
    val currentOnMicRefused by rememberUpdatedState(onMicRefused)
    val holder = remember { arrayOfNulls<CaptureFlow>(1) }
    val mic = micRequest({ holder[0]?.micGranted() }, { holder[0]?.micRefused() })
    val flow =
        remember(mic) {
            CaptureFlow(mic, askConsent = { currentAskConsent() }, onMicRefused = { currentOnMicRefused() }).also { holder[0] = it }
        }
    if (flow.disclosing) CaptureDisclosureDialog(mic.status, onContinue = flow::accept, onCancel = flow::cancel)
    return flow
}

/** The steps of one start, apart from any dialog: what each answer leads to. */
internal class CaptureFlow(
    private val mic: PermissionRequestState,
    private val askConsent: () -> Unit,
    private val onMicRefused: () -> Unit,
) : ExternalCaptureLauncher {
    var disclosing by mutableStateOf(false)
        private set

    // Only a request this flow made goes on to the consent; a grant made elsewhere does not start a capture.
    private var waitingForMic = false

    override fun start() {
        waitingForMic = false
        if (mic.granted && CaptureDisclosure.accepted) askConsent() else disclosing = true
    }

    fun accept() {
        disclosing = false
        CaptureDisclosure.accepted = true
        if (mic.granted) {
            askConsent()
        } else {
            waitingForMic = true
            mic.request()
        }
    }

    fun cancel() {
        disclosing = false
    }

    fun micGranted() {
        if (!waitingForMic) return
        waitingForMic = false
        askConsent()
    }

    /** Shows the disclosure again, now with the refusal and its way back. */
    fun micRefused() {
        if (!waitingForMic) return
        waitingForMic = false
        onMicRefused()
        disclosing = true
    }
}

@Composable
private fun CaptureDisclosureDialog(
    mic: PermissionStatus,
    onContinue: () -> Unit,
    onCancel: () -> Unit,
) {
    PermissionRationales.Shown()
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(stringResource(R.string.ext_disclosure_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.ext_explainer), style = MaterialTheme.typography.bodyMedium)
                val refusal =
                    when (mic) {
                        PermissionStatus.DENIED -> R.string.ext_disclosure_mic_denied
                        PermissionStatus.PERMANENTLY_DENIED -> R.string.ext_disclosure_mic_denied_forever
                        else -> null
                    }
                refusal?.let { Text(stringResource(it), style = MaterialTheme.typography.bodyMedium, color = accentTextColor()) }
            }
        },
        confirmButton = {
            val settings = mic == PermissionStatus.PERMANENTLY_DENIED
            TextButton(onClick = onContinue) {
                Text(stringResource(if (settings) R.string.permission_open_settings else R.string.ext_disclosure_continue))
            }
        },
        dismissButton = { TextButton(onClick = onCancel) { Text(stringResource(R.string.action_cancel)) } },
    )
}

// Android 14+ otherwise offers sharing one app's window, a choice an audio-only capture has no use for.
private fun Context.screenCaptureIntent() =
    getSystemService(MediaProjectionManager::class.java).let { manager ->
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            manager.createScreenCaptureIntent()
        }
    }

private fun Context.granted(permission: String): Boolean =
    ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
