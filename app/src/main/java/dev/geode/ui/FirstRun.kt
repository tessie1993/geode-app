package dev.geode.ui

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CreateNewFolder
import androidx.compose.material.icons.rounded.LibraryMusic
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.ui.theme.GeodeSpacing

/** The audio-read permission for this device's API level. */
internal val audioPermission: String
    get() =
        if (Build.VERSION.SDK_INT >= 33) {
            Manifest.permission.READ_MEDIA_AUDIO
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

/**
 * Supplies [FirstRunSetup] with the library plumbing it needs, so the screen itself stays a
 * function of its arguments and can be looked at without a ViewModel in scope.
 */
@Composable
internal fun FirstRunGate(onDone: () -> Unit) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val roots by libraryViewModel.mediaRoots.collectAsStateWithLifecycle()
    val tracks by libraryViewModel.deviceTracks.collectAsStateWithLifecycle()
    val scanning by libraryViewModel.libraryScanning.collectAsStateWithLifecycle()

    val access = rememberPermissionRequest(audioPermission)
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) libraryViewModel.importFolder(uri)
        }

    // The scan is a CONSEQUENCE of the grant, not a second thing to ask for. Someone who has just
    // said yes to music access has already answered "should I read your library"; making them
    // press a second button to act on their own answer is the app doubting them.
    LaunchedEffect(access.granted) { if (access.granted) libraryViewModel.refreshDeviceTracks() }

    FirstRunSetup(
        access = access.status,
        onRequestAccess = access::request,
        onPickFolder = { folderPicker.launch(null) },
        folderCount = roots.size,
        trackCount = tracks.size,
        scanning = scanning,
        onDone = onDone,
    )
}

/**
 * First run, after the photosensitivity notice has been acknowledged.
 *
 * Two screens, one of them optional, and no questions. It used to end by asking what the person
 * came here to do — listening, making videos, or both — which asked someone to categorise
 * themselves before they had seen a single thing to categorise. Everyone now starts with the
 * whole app, and Settings > Behaviour narrows it for anyone who wants that later.
 *
 * Music access is asked for here, with its reason, because it is the one permission the app is
 * built around. Everything else (microphone, notifications, notification access) is asked for
 * where the feature that needs it is switched on.
 */
@Composable
fun FirstRunSetup(
    access: PermissionStatus,
    onRequestAccess: () -> Unit,
    onPickFolder: () -> Unit,
    folderCount: Int,
    trackCount: Int,
    scanning: Boolean,
    onDone: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // "Not now" on the access screen still leads to the folder step: a folder picked by hand
    // plays without any permission, so it is the way in for someone who said no.
    var accessSkipped by rememberSaveable { mutableStateOf(false) }
    if (access == PermissionStatus.GRANTED || accessSkipped) {
        FolderStep(access, folderCount, trackCount, scanning, onPickFolder, onDone, modifier)
    } else {
        AccessStep(access, onRequestAccess, onSkip = { accessSkipped = true }, modifier = modifier)
    }
}

@Composable
private fun AccessStep(
    access: PermissionStatus,
    onRequestAccess: () -> Unit,
    onSkip: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val body =
        when (access) {
            PermissionStatus.DENIED -> R.string.first_run_access_rationale
            PermissionStatus.PERMANENTLY_DENIED -> R.string.first_run_access_denied
            else -> R.string.first_run_access_body
        }
    val allow =
        when (access) {
            PermissionStatus.DENIED -> R.string.first_run_access_retry
            PermissionStatus.PERMANENTLY_DENIED -> R.string.permission_open_settings
            else -> R.string.first_run_access_allow
        }
    OnboardingScaffold(
        step = 1,
        stepCount = ONBOARDING_STEPS,
        icon = Icons.Rounded.LibraryMusic,
        title = stringResource(R.string.first_run_access_title),
        body = stringResource(body),
        modifier = modifier,
    ) {
        CrystalButton(onClick = onRequestAccess, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(allow))
        }
        // A refusable prompt. The app still opens, still visualises live input, and still plays a
        // folder someone picks by hand, so "not now" is a real answer rather than a dead end.
        CrystalButton(onClick = onSkip, filled = false, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.first_run_access_skip))
        }
    }
}

@Composable
private fun FolderStep(
    access: PermissionStatus,
    folderCount: Int,
    trackCount: Int,
    scanning: Boolean,
    onPickFolder: () -> Unit,
    onDone: () -> Unit,
    modifier: Modifier = Modifier,
) {
    OnboardingScaffold(
        step = 2,
        stepCount = ONBOARDING_STEPS,
        icon = Icons.Rounded.CreateNewFolder,
        title = stringResource(R.string.first_run_folder_title),
        body = stringResource(R.string.first_run_folder_body),
        modifier = modifier,
        extra = {
            Spacer(Modifier.height(GeodeSpacing.L))
            ScanStatus(access = access, scanning = scanning, trackCount = trackCount, folderCount = folderCount)
        },
    ) {
        CrystalButton(onClick = onDone, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.first_run_done))
        }
        CrystalButton(onClick = onPickFolder, filled = false, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.first_run_folder_pick))
        }
    }
}

/**
 * What the app is doing right now, in one line.
 *
 * The track count climbs while the scan runs and stands still when it finishes, which is all
 * anyone needs from this screen — it never blocks on the scan, so this is a reassurance rather
 * than a progress bar someone has to wait out.
 */
@Composable
private fun ScanStatus(
    access: PermissionStatus,
    scanning: Boolean,
    trackCount: Int,
    folderCount: Int,
) {
    val granted = access == PermissionStatus.GRANTED
    // "Android will not ask again" is only true after a permanent refusal; someone who tapped
    // "Not now" hears that the door is still open.
    val line =
        when {
            access == PermissionStatus.PERMANENTLY_DENIED -> stringResource(R.string.first_run_access_denied)
            !granted -> stringResource(R.string.first_run_access_skipped)
            trackCount > 0 -> pluralStringResource(R.plurals.first_run_scanning, trackCount, trackCount)
            scanning -> stringResource(R.string.first_run_scanning_working)
            folderCount > 0 -> pluralStringResource(R.plurals.first_run_folder_added, folderCount, folderCount)
            else -> stringResource(R.string.first_run_access_granted)
        }
    Text(
        line,
        style = MaterialTheme.typography.labelLarge,
        color = if (granted) accentTextColor() else MaterialTheme.colorScheme.onSurfaceVariant,
        textAlign = TextAlign.Center,
    )
}
