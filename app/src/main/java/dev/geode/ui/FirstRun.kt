package dev.geode.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R

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
    val context = LocalContext.current
    val roots by libraryViewModel.mediaRoots.collectAsStateWithLifecycle()
    val tracks by libraryViewModel.deviceTracks.collectAsStateWithLifecycle()
    val scanning by libraryViewModel.libraryScanning.collectAsStateWithLifecycle()

    var granted by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, audioPermission) == PackageManager.PERMISSION_GRANTED,
        )
    }
    var refused by rememberSaveable { mutableStateOf(false) }
    val permissionLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { allowed ->
            granted = allowed
            refused = !allowed
        }
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) libraryViewModel.importFolder(uri)
        }

    // The scan is a CONSEQUENCE of the grant, not a second thing to ask for. Someone who has just
    // said yes to music access has already answered "should I read your library"; making them
    // press a second button to act on their own answer is the app doubting them.
    LaunchedEffect(granted) { if (granted) libraryViewModel.refreshDeviceTracks() }

    FirstRunSetup(
        granted = granted,
        refused = refused,
        onRequestAccess = { permissionLauncher.launch(audioPermission) },
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
 */
@Composable
fun FirstRunSetup(
    granted: Boolean,
    refused: Boolean,
    onRequestAccess: () -> Unit,
    onPickFolder: () -> Unit,
    folderCount: Int,
    trackCount: Int,
    scanning: Boolean,
    onDone: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier =
            modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .systemBarsPadding()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp, vertical = 32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // Access first, and its own screen. The folder step is downstream of it in every sense:
        // it is worded against a library that is already loading, and most people never need it.
        if (!granted && !refused) {
            AccessStep(onRequestAccess = onRequestAccess, onSkip = onDone)
        } else {
            FolderStep(
                granted = granted,
                folderCount = folderCount,
                trackCount = trackCount,
                scanning = scanning,
                onPickFolder = onPickFolder,
                onDone = onDone,
            )
        }
    }
}

@Composable
private fun AccessStep(
    onRequestAccess: () -> Unit,
    onSkip: () -> Unit,
) {
    StepHeading(
        title = stringResource(R.string.first_run_access_title),
        body = stringResource(R.string.first_run_access_body),
    )
    Spacer(Modifier.height(24.dp))
    CrystalButton(onClick = onRequestAccess, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.first_run_access_allow))
    }
    Spacer(Modifier.height(10.dp))
    // A refusable prompt. The app still opens, still visualises live input, and still plays a
    // folder someone picks by hand, so "not now" is a real answer rather than a dead end.
    CrystalButton(onClick = onSkip, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.first_run_access_skip))
    }
}

@Composable
private fun FolderStep(
    granted: Boolean,
    folderCount: Int,
    trackCount: Int,
    scanning: Boolean,
    onPickFolder: () -> Unit,
    onDone: () -> Unit,
) {
    CrystalOverline(stringResource(R.string.first_run_optional))
    Spacer(Modifier.height(8.dp))
    StepHeading(
        title = stringResource(R.string.first_run_folder_title),
        body = stringResource(R.string.first_run_folder_body),
    )
    Spacer(Modifier.height(16.dp))
    ScanStatus(granted = granted, scanning = scanning, trackCount = trackCount, folderCount = folderCount)
    Spacer(Modifier.height(20.dp))
    CrystalButton(onClick = onPickFolder, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.first_run_folder_pick))
    }
    Spacer(Modifier.height(10.dp))
    CrystalButton(onClick = onDone, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.first_run_done))
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
    granted: Boolean,
    scanning: Boolean,
    trackCount: Int,
    folderCount: Int,
) {
    val line =
        when {
            !granted -> stringResource(R.string.first_run_access_denied)
            trackCount > 0 -> pluralStringResource(R.plurals.first_run_scanning, trackCount, trackCount)
            scanning -> stringResource(R.string.first_run_scanning_working)
            folderCount > 0 -> pluralStringResource(R.plurals.first_run_folder_added, folderCount, folderCount)
            else -> stringResource(R.string.first_run_access_granted)
        }
    Text(
        line,
        style = MaterialTheme.typography.labelMedium,
        color = if (granted) accentTextColor() else MaterialTheme.colorScheme.onSurfaceVariant,
        textAlign = TextAlign.Center,
    )
}

@Composable
private fun StepHeading(
    title: String,
    body: String,
) {
    Text(
        title,
        style = MaterialTheme.typography.headlineSmall,
        color = MaterialTheme.colorScheme.onBackground,
        textAlign = TextAlign.Center,
        modifier = Modifier.semantics { heading() },
    )
    Spacer(Modifier.height(12.dp))
    Text(
        body,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        textAlign = TextAlign.Center,
    )
}
