package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
internal fun FolderSettingsTab() {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.folders_group_preset)) { PresetFolderGroup(settingsViewModel, gui) } }
        item { SettingsGroup(stringResource(R.string.folders_group_music)) { MusicFoldersEditor(libraryViewModel) } }
        item { SettingsGroup(stringResource(R.string.folders_group_cache)) { AnalysisCacheGroup() } }
        item {
            SettingsGroup(stringResource(R.string.folders_group_export)) {
                Text(
                    stringResource(R.string.folders_export_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun PresetFolderGroup(
    viewModel: SettingsViewModel,
    gui: GuiPrefs,
) {
    val ctx = LocalContext.current
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) {
                val persisted =
                    runCatching {
                        ctx.contentResolver.takePersistableUriPermission(
                            uri,
                            android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or
                                android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
                        )
                    }.isSuccess
                if (persisted) viewModel.setGuiPrefs(gui.copy(presetMirrorUri = uri.toString()))
            }
        }
    Column {
        Text(
            stringResource(
                if (gui.presetMirrorUri != null) {
                    R.string.folders_preset_chosen
                } else {
                    R.string.folders_preset_internal
                },
            ),
            style = MaterialTheme.typography.labelMedium,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(filled = false, onClick = { folderPicker.launch(null) }) { Text(stringResource(R.string.folders_choose_preset)) }
            if (gui.presetMirrorUri != null) {
                TextButton(
                    onClick = { viewModel.setGuiPrefs(gui.copy(presetMirrorUri = null)) },
                ) { Text(stringResource(R.string.action_clear)) }
            }
        }
        Text(
            stringResource(R.string.folders_preset_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
internal fun MusicFoldersEditor(viewModel: LibraryViewModel) {
    val roots by viewModel.mediaRoots.collectAsStateWithLifecycle()
    val scanning by viewModel.libraryScanning.collectAsStateWithLifecycle()
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) viewModel.importFolder(uri)
        }
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (roots.isEmpty()) {
            Text(
                stringResource(R.string.folders_none_yet),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        roots.sorted().forEach { root ->
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    java.net.URLDecoder
                        .decode(root.substringAfterLast("%3A").substringAfterLast("/"), "UTF-8")
                        .ifBlank { root },
                    Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
                IconButton(onClick = { viewModel.removeMediaRoot(root) }) {
                    StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.folders_remove))
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(
                compact = true,
                filled = false,
                onClick = { folderPicker.launch(null) },
            ) { Text(stringResource(R.string.folders_add)) }
            CrystalButton(
                compact = true,
                filled = false,
                onClick = viewModel::rescanMediaRoots,
                enabled = roots.isNotEmpty() && !scanning,
            ) {
                Text(stringResource(if (scanning) R.string.folders_scanning else R.string.folders_rescan))
            }
        }
        Text(
            stringResource(R.string.folders_rescan_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun AnalysisCacheGroup() {
    val context = LocalContext.current
    val measuring = stringResource(R.string.folders_cache_measuring)
    var cacheInfo by remember { mutableStateOf(measuring) }
    var cacheBump by remember { mutableIntStateOf(0) }
    LaunchedEffect(cacheBump) {
        cacheInfo =
            withContext(Dispatchers.IO) {
                val app = context.applicationContext
                val n =
                    dev.geode.analysis.AnalysisCache
                        .entryCount(app)
                val mb =
                    dev.geode.analysis.AnalysisCache
                        .sizeBytes(app) / (1024f * 1024f)
                context.getString(R.string.folders_cache_summary, n, mb)
            }
    }
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            stringResource(R.string.folders_cache_label, cacheInfo),
            Modifier.weight(1f),
            style = MaterialTheme.typography.bodySmall,
        )
        TextButton(onClick = {
            dev.geode.analysis.AnalysisCache
                .clear(context.applicationContext)
            cacheBump++
        }) { Text(stringResource(R.string.action_clear)) }
    }
    Text(
        stringResource(R.string.folders_cache_explainer),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
