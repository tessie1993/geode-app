package dev.geode.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.SaveTarget

/**
 * The ask-once question after the first recording, which has nowhere to go yet
 * ([RecordUiState.pendingSave]): the Videos library (Movies/BeatRealms, no permission) or a folder
 * the user picks, and whether to keep that answer for every later recording (on by default). A kept
 * answer (`data/RecordSaveStore`) means later recordings are written straight there; an answer not
 * kept places this one only, and the next stop asks again.
 *
 * Stateless apart from the checkbox: [RecordSaveHost] shows it while [title] is non-null and passes
 * the answer to `RecordController.chooseSaveTarget`; dismissing it is `skipPendingSave`, which puts
 * this recording in the Videos library. The folder picker hands back a tree URI whose persistable
 * grant the store takes either way.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecordSaveSheet(
    title: String?,
    onChoose: (target: SaveTarget, remember: Boolean) -> Unit,
    onSkip: () -> Unit,
) {
    if (title == null) return
    var keep by rememberSaveable { mutableStateOf(true) }
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
            // A cancelled picker leaves the sheet up, so the user can still choose the library.
            if (uri != null) onChoose(SaveTarget.Folder(uri.toString()), keep)
        }
    ModalBottomSheet(onDismissRequest = onSkip) {
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.record_save_title), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.record_save_body, title),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CrystalButton(onClick = { onChoose(SaveTarget.VideosLibrary, keep) }, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.record_save_library))
            }
            Text(
                stringResource(R.string.record_save_library_hint),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CrystalButton(filled = false, onClick = { folderPicker.launch(null) }, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.record_save_folder))
            }
            // One row is the toggle target (the whole line, Role.Checkbox), so TalkBack reads it
            // as one control and the box is never a tiny tap target on its own.
            Row(
                Modifier
                    .fillMaxWidth()
                    .toggleable(value = keep, role = Role.Checkbox, onValueChange = { keep = it }),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Checkbox(checked = keep, onCheckedChange = null)
                Text(stringResource(R.string.record_save_remember), style = MaterialTheme.typography.bodyMedium)
            }
            TextButton(onClick = onSkip) { Text(stringResource(R.string.record_save_later)) }
        }
    }
}

/**
 * Draws [RecordSaveSheet] for the recording waiting to be placed, wherever the user is when it
 * stops: from `AppRoot`, beside the paywall, rather than inside one screen, since a recording also
 * ends on its own (its maximum length, a full file) while the user may be elsewhere.
 */
@Composable
fun RecordSaveHost() {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val record by studioViewModel.recordState.collectAsStateWithLifecycle()
    RecordSaveSheet(
        title = record.pendingSave,
        onChoose = studioViewModel::chooseRecordSaveTarget,
        onSkip = studioViewModel::skipRecordSave,
    )
}
