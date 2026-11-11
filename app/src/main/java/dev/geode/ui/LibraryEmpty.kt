package dev.geode.ui

import android.app.Activity
import android.media.projection.MediaProjectionManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.PlaybackCaptureService

/**
 * What the library says when music access is granted and MediaStore has no songs (#340).
 *
 * "No music found" alone is a dead end, and the three ways out are different problems: the songs
 * were just copied and Android has not indexed them yet (Rescan), they live in a folder MediaStore
 * does not call music (choose the folder), or there is no music on the phone at all and the
 * visuals should listen to something else (another app or the mic). Stateless, so a UI test can
 * draw it alone; [LibraryEmptyHost] wires it to the app.
 *
 * [minTrackSeconds] above zero adds one line saying short files are hidden, since that filter is
 * the other way songs can go missing.
 */
@Composable
internal fun LibraryEmpty(
    onRescan: () -> Unit,
    onChooseFolder: () -> Unit,
    onListenToApp: () -> Unit,
    onListenToMic: () -> Unit,
    modifier: Modifier = Modifier,
    minTrackSeconds: Int = 0,
) {
    var listenMenu by remember { mutableStateOf(false) }
    Column(
        modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            stringResource(R.string.library_empty_title),
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.semantics { heading() },
        )
        Text(
            stringResource(R.string.library_empty_hint),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (minTrackSeconds > 0) {
            Text(
                stringResource(R.string.library_empty_short_hint, minTrackSeconds),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        val buttonWidth = Modifier.widthIn(min = 240.dp)
        FilledTonalButton(onClick = onRescan, modifier = buttonWidth) {
            Text(stringResource(R.string.library_empty_rescan))
        }
        OutlinedButton(onClick = onChooseFolder, modifier = buttonWidth) {
            Text(stringResource(R.string.library_empty_choose_folder))
        }
        Box {
            OutlinedButton(
                onClick = { listenMenu = true },
                modifier = buttonWidth,
            ) { Text(stringResource(R.string.library_empty_listen)) }
            DropdownMenu(expanded = listenMenu, onDismissRequest = { listenMenu = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.library_empty_listen_app)) },
                    onClick = {
                        listenMenu = false
                        onListenToApp()
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.library_empty_listen_mic)) },
                    onClick = {
                        listenMenu = false
                        onListenToMic()
                    },
                )
            }
        }
    }
}

/**
 * [LibraryEmpty] wired to the app: Rescan re-asks MediaStore (and re-walks any chosen folders),
 * the folder button opens Settings › Folders, and listening starts the capture the Player's quick
 * actions start (same permission and consent steps) and then opens Visuals, where the sound is.
 */
@Composable
internal fun LibraryEmptyHost(
    libraryViewModel: LibraryViewModel,
    playerViewModel: PlayerViewModel,
    onRescan: () -> Unit,
    onChooseFolder: () -> Unit,
    onOpenVisuals: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val external by playerViewModel.externalAudio.collectAsStateWithLifecycle()
    val minTrackSeconds by libraryViewModel.minTrackSeconds.collectAsStateWithLifecycle()
    val micPermission =
        rememberPermissionRequest(android.Manifest.permission.RECORD_AUDIO) {
            if (playerViewModel.setMicEnabled(true) == null) onOpenVisuals()
        }
    val projectionLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val data = result.data
            if (result.resultCode == Activity.RESULT_OK && data != null) {
                PlaybackCaptureService.start(context, result.resultCode, data)
                onOpenVisuals()
            } else {
                playerViewModel.noteExternalAudioConsentDenied()
            }
        }
    val capturePermissions =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
            if (granted[android.Manifest.permission.RECORD_AUDIO] != false) {
                playerViewModel.noteExternalAudioConsentPending()
                projectionLauncher.launch(context.getSystemService(MediaProjectionManager::class.java).createScreenCaptureIntent())
            } else {
                playerViewModel.noteExternalAudioConsentDenied()
            }
        }
    LibraryEmpty(
        onRescan = onRescan,
        onChooseFolder = onChooseFolder,
        onListenToApp = {
            if (external.active) {
                onOpenVisuals()
            } else {
                capturePermissions.launch(
                    arrayOf(android.Manifest.permission.RECORD_AUDIO, android.Manifest.permission.POST_NOTIFICATIONS),
                )
            }
        },
        onListenToMic = {
            // A mic that fails to open keeps the user here rather than on a visual with nothing to hear.
            if (playerViewModel.hasMicPermission()) {
                if (playerViewModel.setMicEnabled(true) == null) onOpenVisuals()
            } else {
                micPermission.request()
            }
        },
        modifier = modifier,
        minTrackSeconds = minTrackSeconds,
    )
}
