package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material3.Card
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.collapse
import androidx.compose.ui.semantics.expand
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.DeviceTrack
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

@Composable
internal fun FoldersTab(
    folders: Map<String, List<DeviceTrack>>,
    viewModel: PlayerViewModel,
    wide: Boolean = false,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val folderRows = remember(folders) { FolderTree.rows(folders) }
    val deviceFolders: @Composable (Modifier) -> Unit = { modifier ->
        Column(modifier) {
            Text(
                stringResource(R.string.folders_device),
                Modifier.padding(horizontal = 16.dp, vertical = 4.dp).semantics { heading() },
                style = MaterialTheme.typography.titleSmall,
            )
            GroupList(folderRows, viewModel, libraryViewModel)
        }
    }
    // The library folders are tools, the device folders are the content: the tools fold into one
    // card with a summary, so the content starts near the top, and a wide pane sets them side by side.
    if (wide) {
        Row(Modifier.fillMaxSize()) {
            Column(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState())) {
                LibraryFoldersCard(libraryViewModel)
            }
            deviceFolders(Modifier.weight(1f).fillMaxHeight())
        }
    } else {
        Column {
            LibraryFoldersCard(libraryViewModel)
            deviceFolders(Modifier)
        }
    }
}

/**
 * Library › Folders' own tools as one collapsible card: the name, a one-line summary (the folders'
 * names, or Scanning… while a rescan runs) and a ▾ that turns when open. Collapsed by default,
 * like every V2 options card: the folder list and its buttons are needed now and then, the device
 * folders under it every time. TalkBack hears it as expandable through the expand and collapse
 * actions.
 */
@Composable
private fun LibraryFoldersCard(libraryViewModel: LibraryViewModel) {
    val roots by libraryViewModel.mediaRoots.collectAsStateWithLifecycle()
    val scanning by libraryViewModel.libraryScanning.collectAsStateWithLifecycle()
    val context = androidx.compose.ui.platform.LocalContext.current
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) libraryViewModel.importFolder(uri)
        }
    var open by rememberSaveable { mutableStateOf(false) }
    val motion = LocalGeodeMotion.current
    val chevron by animateFloatAsState(if (open) 180f else 0f, motion.defaultSpatialSpec(), label = "foldersChevron")
    val named =
        remember(roots) {
            roots.sorted().map { root ->
                root to
                    java.net.URLDecoder
                        .decode(root.substringAfterLast("%3A").substringAfterLast("/"), "UTF-8")
                        .ifBlank { root }
            }
        }
    val summary =
        when {
            scanning -> stringResource(R.string.folders_scanning)
            named.isEmpty() -> "0"
            else -> named.joinToString(", ") { it.second }
        }
    Card(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = 48.dp)
                .clickable(role = Role.Button) { open = !open }
                .semantics {
                    if (open) {
                        collapse {
                            open = false
                            true
                        }
                    } else {
                        expand {
                            open = true
                            true
                        }
                    }
                }.padding(start = 16.dp, end = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                stringResource(R.string.folders_library),
                style = MaterialTheme.typography.titleSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                summary,
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.End,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(Icons.Filled.KeyboardArrowDown, contentDescription = null, Modifier.graphicsLayer { rotationZ = chevron })
        }
        AnimatedVisibility(
            visible = open,
            enter = expandVertically(motion.defaultSpatialSpec()) + fadeIn(motion.defaultEffectsSpec()),
            exit = shrinkVertically(motion.defaultSpatialSpec()) + fadeOut(motion.defaultEffectsSpec()),
        ) {
            Column(Modifier.padding(bottom = 8.dp)) {
                named.forEach { (root, name) ->
                    Row(
                        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            name,
                            Modifier.weight(1f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            style = MaterialTheme.typography.bodySmall,
                        )
                        IconButton(onClick = { libraryViewModel.removeMediaRoot(root) }) {
                            StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.folders_remove))
                        }
                    }
                }
                Row(
                    Modifier.padding(horizontal = 16.dp, vertical = 2.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    FilledTonalButton(
                        onClick = { folderPicker.launchPicker(context, null) },
                        contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
                    ) { Text(stringResource(R.string.folders_add)) }
                    FilledTonalButton(
                        onClick = libraryViewModel::rescanMediaRoots,
                        enabled = roots.isNotEmpty() && !scanning,
                        contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
                    ) {
                        Text(stringResource(if (scanning) R.string.folders_scanning else R.string.folders_rescan))
                    }
                }
            }
        }
    }
}
