package dev.geode.ui

import android.content.Context
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.backup.BackupFormat
import dev.geode.data.backup.BackupProblem
import dev.geode.data.backup.BackupSection
import java.text.DateFormat
import java.time.LocalDate
import java.util.Date
import kotlin.system.exitProcess

/** Zip files arrive under any of these types depending on the app that saved them. */
private val zipMimeTypes = arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")

@Composable
internal fun BackupSettingsTab() {
    val viewModel: BackupViewModel = geodeViewModel()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    val exportPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
            if (uri != null) viewModel.export(uri)
        }
    val importPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) viewModel.inspect(uri)
        }
    val idle = state != BackupUiState.Working && state != BackupUiState.Restored
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.backup_group)) {
                Text(
                    stringResource(R.string.backup_body),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CrystalButton(
                        onClick = { exportPicker.launch(BackupFormat.defaultFileName(LocalDate.now())) },
                        modifier = Modifier.weight(1f),
                        enabled = idle,
                    ) { Text(stringResource(R.string.backup_export)) }
                    CrystalButton(
                        onClick = { importPicker.launch(zipMimeTypes) },
                        modifier = Modifier.weight(1f),
                        enabled = idle,
                        filled = false,
                    ) { Text(stringResource(R.string.backup_import)) }
                }
                BackupStatus(state, onDismiss = viewModel::dismiss)
            }
        }
    }
    when (val current = state) {
        is BackupUiState.Choosing -> RestoreChoiceDialog(current, viewModel)
        BackupUiState.Restored -> RestartDialog()
        else -> Unit
    }
}

@Composable
private fun BackupStatus(
    state: BackupUiState,
    onDismiss: () -> Unit,
) {
    val (text, isError) =
        when (state) {
            BackupUiState.Working -> stringResource(R.string.backup_working) to false
            BackupUiState.Exported -> stringResource(R.string.backup_exported) to false
            is BackupUiState.Failed -> stringResource(problemText(state.problem)) to true
            else -> return
        }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            text,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.bodySmall,
            color = if (isError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (state != BackupUiState.Working) {
            CrystalButton(onClick = onDismiss, filled = false, compact = true) { Text(stringResource(R.string.action_dismiss)) }
        }
    }
}

@Composable
private fun RestoreChoiceDialog(
    choosing: BackupUiState.Choosing,
    viewModel: BackupViewModel,
) {
    val madeOn = DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(choosing.preview.createdAtMs))
    AlertDialog(
        onDismissRequest = viewModel::cancelRestore,
        title = { Text(stringResource(R.string.backup_restore_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(stringResource(R.string.backup_restore_body, madeOn), style = MaterialTheme.typography.bodySmall)
                for (section in BackupSection.entries.filter { it in choosing.preview.sections }) {
                    val checked = section in choosing.selected
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .toggleable(value = checked, role = Role.Checkbox, onValueChange = { viewModel.toggle(section) }),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Checkbox(checked = checked, onCheckedChange = null)
                        Text(stringResource(sectionText(section)), style = MaterialTheme.typography.bodyMedium)
                    }
                }
            }
        },
        confirmButton = {
            CrystalButton(onClick = viewModel::restore, enabled = choosing.selected.isNotEmpty()) {
                Text(stringResource(R.string.backup_restore_confirm))
            }
        },
        dismissButton = {
            CrystalButton(onClick = viewModel::cancelRestore, filled = false) { Text(stringResource(R.string.action_cancel)) }
        },
    )
}

/**
 * Not dismissable: every store read its data at startup and still holds the old copy, and the
 * next save from any of them would write it back over the restore.
 */
@Composable
private fun RestartDialog() {
    val context = LocalContext.current
    AlertDialog(
        onDismissRequest = {},
        title = { Text(stringResource(R.string.backup_restored_title)) },
        text = { Text(stringResource(R.string.backup_restored_body)) },
        confirmButton = {
            CrystalButton(onClick = { restartApp(context) }) { Text(stringResource(R.string.backup_restart)) }
        },
    )
}

private fun restartApp(context: Context) {
    val launch = context.packageManager.getLaunchIntentForPackage(context.packageName) ?: return
    context.startActivity(Intent.makeRestartActivityTask(launch.component))
    exitProcess(0)
}

private fun sectionText(section: BackupSection): Int =
    when (section) {
        BackupSection.SETTINGS -> R.string.backup_section_settings
        BackupSection.CREATIVE -> R.string.backup_section_creative
        BackupSection.MUSIC -> R.string.backup_section_music
        BackupSection.MILK -> R.string.backup_section_milk
    }

private fun problemText(problem: BackupProblem): Int =
    when (problem) {
        BackupProblem.NOT_A_BACKUP -> R.string.backup_error_not_a_backup
        BackupProblem.NEWER_VERSION -> R.string.backup_error_newer_version
        BackupProblem.UNSAFE_PATH -> R.string.backup_error_unsafe_path
        BackupProblem.DAMAGED -> R.string.backup_error_damaged
        BackupProblem.TOO_LARGE -> R.string.backup_error_too_large
        BackupProblem.IO_FAILED -> R.string.backup_error_io
    }
