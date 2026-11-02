package dev.geode.ui

import android.text.format.DateUtils
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.editor.ProjectId
import dev.geode.editor.StudioProject
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.floatingGlass

private const val RECENT_COUNT = 5

/**
 * Studio › Edit's home, the way a desktop or phone editor opens: start something new, pick up a
 * recent project, or find any project. Every way in ends in [onOpen], which hands Studio's whole
 * screen to the open project's timeline.
 *
 * New from:
 * - what's playing: the song on an audio lane and the look on stage as one visual clip over it;
 * - a take: takes are projects already (a stopped recording makes one), so this opens it;
 * - a template: the template fitted to the song playing;
 * - a video on the phone: the video on a media lane;
 * - blank: an empty timeline.
 */
@Composable
internal fun EditHome(
    viewModel: StudioViewModel,
    player: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpen: () -> Unit,
) {
    val editor by viewModel.editor.collectAsStateWithLifecycle()
    val takes by viewModel.takeState.collectAsStateWithLifecycle()
    val playerState by player.uiState.collectAsStateWithLifecycle()
    val viz by player.vizState.collectAsStateWithLifecycle()
    val track = viewModel.currentTrack(playerState.title).takeIf { playerState.hasMedia }
    var picking by rememberSaveable { mutableStateOf<EditPick?>(null) }
    var query by rememberSaveable { mutableStateOf("") }
    var byName by rememberSaveable { mutableStateOf(false) }
    var renaming by remember { mutableStateOf<StudioProject?>(null) }
    var deleting by remember { mutableStateOf<StudioProject?>(null) }
    val openProject: (ProjectId) -> Unit = {
        viewModel.openProject(it)
        onOpen()
    }
    val videoPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                viewModel.newProjectFromVideo(uri)
                onOpen()
            }
        }
    val all =
        editor.projects
            .filter { query.isBlank() || it.name.contains(query.trim(), ignoreCase = true) }
            .let { list -> if (byName) list.sortedBy { it.name.lowercase() } else list.sortedByDescending { it.updatedAtMs } }

    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        item {
            EditSection(stringResource(R.string.edit_home_new)) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    val fromPlaying: () -> Unit = {
                        if (track != null) {
                            val milkPath = visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId).milkPresetPath
                            viewModel.newProjectFromPlaying(track, milkPath)
                            onOpen()
                        }
                    }
                    val blank: () -> Unit = {
                        viewModel.newBlankProject()
                        onOpen()
                    }
                    CrystalButton(enabled = track != null, onClick = fromPlaying) {
                        Text(stringResource(R.string.edit_home_from_playing))
                    }
                    CrystalButton(filled = false, enabled = takes.takes.isNotEmpty(), onClick = { picking = EditPick.TAKE }) {
                        Text(stringResource(R.string.edit_home_from_take))
                    }
                    CrystalButton(
                        filled = false,
                        enabled = track != null && editor.templates.isNotEmpty(),
                        onClick = { picking = EditPick.TEMPLATE },
                    ) { Text(stringResource(R.string.edit_home_from_template)) }
                    CrystalButton(filled = false, onClick = { videoPicker.launch(arrayOf("video/*")) }) {
                        Text(stringResource(R.string.edit_home_from_video))
                    }
                    CrystalButton(filled = false, onClick = blank) { Text(stringResource(R.string.edit_home_blank)) }
                }
            }
        }
        val recent = editor.projects.sortedByDescending { it.updatedAtMs }.take(RECENT_COUNT)
        if (recent.isNotEmpty()) {
            item {
                EditSection(stringResource(R.string.edit_home_recent)) {
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        items(recent, key = { it.id.value }) { project ->
                            Column(
                                Modifier
                                    .width(160.dp)
                                    .floatingGlass()
                                    .clickable { openProject(project.id) }
                                    .padding(12.dp),
                            ) {
                                Text(
                                    project.name,
                                    style = MaterialTheme.typography.titleSmall,
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis,
                                )
                                EditHint(projectSummary(project))
                            }
                        }
                    }
                }
            }
        }
        item {
            EditSection(stringResource(R.string.edit_home_all)) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    placeholder = { Text(stringResource(R.string.edit_home_search)) },
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(
                        selected = !byName,
                        onClick = { byName = false },
                        label = { Text(stringResource(R.string.edit_home_sort_edited)) },
                    )
                    FilterChip(
                        selected = byName,
                        onClick = { byName = true },
                        label = { Text(stringResource(R.string.edit_home_sort_name)) },
                    )
                }
                if (all.isEmpty()) EditHint(stringResource(R.string.edit_home_empty))
            }
        }
        items(all, key = { it.id.value }) { project ->
            ProjectRow(
                project = project,
                onOpen = { openProject(project.id) },
                onRename = { renaming = project },
                onDelete = { deleting = project },
            )
        }
    }

    when (picking) {
        EditPick.TAKE ->
            PickDialog(
                title = stringResource(R.string.edit_home_from_take),
                names = takes.takes.map { it.name },
                onPick = { i ->
                    picking = null
                    openProject(takes.takes[i].projectId)
                },
                onDismiss = { picking = null },
            )
        EditPick.TEMPLATE ->
            PickDialog(
                title = stringResource(R.string.edit_home_from_template),
                names = editor.templates.map { it.name },
                onPick = { i ->
                    picking = null
                    track?.let {
                        viewModel.newProjectFromTemplate(editor.templates[i].id, it)
                        onOpen()
                    }
                },
                onDismiss = { picking = null },
            )
        null -> Unit
    }
    renaming?.let { project ->
        RenameProjectDialog(
            initial = project.name,
            onRename = { name ->
                viewModel.renameProject(project.id, name)
                renaming = null
            },
            onDismiss = { renaming = null },
        )
    }
    deleting?.let { project ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(stringResource(R.string.edit_home_delete_title, project.name)) },
            text = { Text(stringResource(R.string.edit_home_delete_body)) },
            confirmButton = {
                val confirm: () -> Unit = {
                    viewModel.deleteProject(project.id)
                    deleting = null
                }
                TextButton(onClick = confirm) { Text(stringResource(R.string.edit_home_delete)) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(android.R.string.cancel)) } },
        )
    }
}

private enum class EditPick { TAKE, TEMPLATE }

@Composable
private fun EditSection(
    title: String,
    content: @Composable () -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
        content()
    }
}

@Composable
private fun EditHint(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** "2:31 · 3 videos · 5 minutes ago": length, how many renders it holds, when it last changed. */
@Composable
private fun projectSummary(project: StudioProject): String =
    listOfNotNull(
        formatClock(project.lengthMs),
        pluralStringResource(R.plurals.edit_home_videos, project.renders.size, project.renders.size)
            .takeIf { project.renders.isNotEmpty() },
        DateUtils.getRelativeTimeSpanString(project.updatedAtMs).toString(),
    ).joinToString(" · ")

@Composable
private fun ProjectRow(
    project: StudioProject,
    onOpen: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onOpen)
            .padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(project.name, style = MaterialTheme.typography.bodyLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
            EditHint(projectSummary(project))
        }
        IconButton(onClick = { menu = true }) {
            Icon(Icons.Filled.MoreVert, contentDescription = stringResource(R.string.edit_home_more))
        }
        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.edit_home_rename)) },
                onClick = {
                    menu = false
                    onRename()
                },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.edit_home_delete)) },
                onClick = {
                    menu = false
                    onDelete()
                },
            )
        }
    }
}

@Composable
private fun PickDialog(
    title: String,
    names: List<String>,
    onPick: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            LazyColumn {
                items(names.size) { i ->
                    Text(
                        names[i],
                        Modifier
                            .fillMaxWidth()
                            .clickable { onPick(i) }
                            .padding(vertical = 12.dp),
                    )
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) } },
    )
}

@Composable
private fun RenameProjectDialog(
    initial: String,
    onRename: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by rememberSaveable { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.edit_home_rename)) },
        text = { OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true) },
        confirmButton = {
            TextButton(enabled = name.isNotBlank(), onClick = { onRename(name) }) { Text(stringResource(R.string.edit_home_rename)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) } },
    )
}
