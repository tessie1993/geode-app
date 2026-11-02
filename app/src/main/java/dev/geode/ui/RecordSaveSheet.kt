package dev.geode.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.SaveTarget

/**
 * The ask-once question after the first recorded take ([TakeUiState.pendingSave]): render to the
 * Videos library (Movies/MuzViz, no permission) or to a folder the user picks. The answer is kept
 * (`data/RecordSaveStore`), so later takes render without this sheet.
 *
 * Stateless: the host that owns Record (the Visuals stage, its long press) shows it while
 * [takeName] is non-null and passes the answer to `TakeController.chooseSaveTarget`; dismissing it
 * is `skipPendingSave`, which leaves the take in My videos, unrendered. The folder picker hands back
 * a tree URI whose persistable grant the store takes.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecordSaveSheet(
    takeName: String?,
    onChoose: (SaveTarget) -> Unit,
    onSkip: () -> Unit,
) {
    if (takeName == null) return
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
            // A cancelled picker leaves the sheet up, so the user can still choose the library.
            if (uri != null) onChoose(SaveTarget.Folder(uri.toString()))
        }
    ModalBottomSheet(onDismissRequest = onSkip) {
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.record_save_title), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.record_save_body, takeName),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            CrystalButton(onClick = { onChoose(SaveTarget.VideosLibrary) }, modifier = Modifier.fillMaxWidth()) {
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
            TextButton(onClick = onSkip) { Text(stringResource(R.string.record_save_later)) }
        }
    }
}
