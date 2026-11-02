package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.performStoneHaptic

/**
 * Visuals › MilkDrop › Textures: the images MilkDrop presets can sample, imported by the user. A
 * plain list (V2: one row per image, its actions in the row's ⋮ rather than buttons on every row):
 * Use loads a preset that shows the image, Delete removes it after a confirm. Import sits on top.
 */
@Composable
internal fun TexturesHubTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val textures by visualsViewModel.textures.collectAsStateWithLifecycle()
    var deletingTexture by remember { mutableStateOf<String?>(null) }
    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isNotEmpty()) {
                visualsViewModel.importTextures(uris) { visualizerView.visualizerRenderer.reloadCurrentMilkPreset() }
            }
        }
    LazyColumn(
        Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XXS),
    ) {
        item {
            Row(
                Modifier.fillMaxWidth().padding(GeodeSpacing.M),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.weight(1f)) {
                    if (textures.isEmpty()) {
                        Text(
                            stringResource(R.string.textures_none),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                FilledTonalButton(onClick = { picker.launch(arrayOf("image/*")) }) {
                    StoneIconArt(StoneIcon.ADD, null)
                    Text(stringResource(R.string.textures_import), Modifier.padding(start = GeodeSpacing.S))
                }
            }
        }
        items(textures, key = { "texture_${it.name}" }) { tex ->
            TextureRow(
                name = tex.name,
                onUse = { visualsViewModel.useTexture(tex.name) { path -> selectMilk(viewModel, visualizerView, path) } },
                onDelete = { deletingTexture = tex.name },
            )
        }
    }
    deletingTexture?.let { name ->
        AlertDialog(
            onDismissRequest = { deletingTexture = null },
            title = { Text(stringResource(R.string.textures_delete_title, name)) },
            text = { Text(stringResource(R.string.textures_delete_body)) },
            confirmButton = {
                TextButton(onClick = {
                    visualsViewModel.removeTexture(name)
                    deletingTexture = null
                }) { Text(stringResource(R.string.action_delete), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = {
                TextButton(onClick = { deletingTexture = null }) { Text(stringResource(R.string.action_cancel)) }
            },
        )
    }
}

/** One image: its name, and Use and Delete in its ⋮. */
@Composable
private fun TextureRow(
    name: String,
    onUse: () -> Unit,
    onDelete: () -> Unit,
) {
    val view = LocalView.current
    var menuOpen by remember { mutableStateOf(false) }
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .padding(start = GeodeSpacing.L),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(name, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Box {
            IconButton(onClick = {
                view.performStoneHaptic(StoneHapticCue.TAP)
                menuOpen = true
            }) { StoneIconArt(StoneIcon.MORE, stringResource(R.string.textures_more, name)) }
            DropdownMenu(menuOpen, onDismissRequest = { menuOpen = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.textures_use)) },
                    leadingIcon = { StoneIconArt(StoneIcon.CHECK, null) },
                    onClick = {
                        menuOpen = false
                        onUse()
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.action_delete)) },
                    leadingIcon = { StoneIconArt(StoneIcon.DELETE, null, tint = MaterialTheme.colorScheme.error) },
                    onClick = {
                        menuOpen = false
                        onDelete()
                    },
                )
            }
        }
    }
}
