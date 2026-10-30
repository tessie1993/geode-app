package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.export.BatchTrack
import dev.geode.export.RenderScope
import dev.geode.ui.pro.ProBadge

/** A playlist the Render tab can batch: saved or smart, already resolved to its tracks. */
internal data class RenderPlaylist(
    val name: String,
    val tracks: List<BatchTrack>,
)

@Composable
internal fun renderScopeLabel(scope: RenderScope): String =
    stringResource(
        when (scope) {
            RenderScope.CURRENT_TRACK -> R.string.render_scope_current
            RenderScope.PLAY_QUEUE -> R.string.render_scope_queue
            RenderScope.PLAYLIST -> R.string.render_scope_playlist
            RenderScope.CHOSEN_TRACKS -> R.string.render_scope_chosen
        },
    )

/**
 * The Render bar's "what does Render cover" control: the scope's name and what it resolves to
 * (the playing track, "12 tracks", the playlist's name). Tapping it lists the four scopes; the two
 * that need a choice open their picker as they are picked. Batches show the PRO badge for users
 * without it; picking one is allowed, and Render then opens the paywall (PlayerViewModel.startBatch).
 */
@Composable
internal fun RenderScopeChooser(
    scope: RenderScope,
    summary: String,
    batchLocked: Boolean,
    onScope: (RenderScope) -> Unit,
    onPickPlaylist: () -> Unit,
    onPickTracks: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        Column(Modifier.clickable(role = Role.DropdownList) { open = true }) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(renderScopeLabel(scope), style = MaterialTheme.typography.labelMedium)
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
            }
            Text(
                summary,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            RenderScope.entries.forEach { option ->
                DropdownMenuItem(
                    text = {
                        Text(
                            when (option) {
                                RenderScope.PLAYLIST -> stringResource(R.string.render_scope_pick_playlist)
                                RenderScope.CHOSEN_TRACKS -> stringResource(R.string.render_scope_pick_tracks)
                                else -> renderScopeLabel(option)
                            },
                        )
                    },
                    trailingIcon = { if (option.isBatch && batchLocked) ProBadge() },
                    onClick = {
                        open = false
                        onScope(option)
                        when (option) {
                            RenderScope.PLAYLIST -> onPickPlaylist()
                            RenderScope.CHOSEN_TRACKS -> onPickTracks()
                            RenderScope.CURRENT_TRACK, RenderScope.PLAY_QUEUE -> Unit
                        }
                    },
                )
            }
        }
    }
}

@Composable
internal fun PlaylistPickerDialog(
    playlists: List<RenderPlaylist>,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.render_scope_pick_playlist)) },
        text = {
            if (playlists.isEmpty()) {
                Text(stringResource(R.string.render_no_playlists))
            } else {
                LazyColumn(Modifier.heightIn(max = 420.dp)) {
                    items(playlists, key = { it.name }) { playlist ->
                        Column(
                            Modifier
                                .fillMaxWidth()
                                .clickable { onPick(playlist.name) }
                                .padding(vertical = 10.dp),
                        ) {
                            Text(playlist.name, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                pluralStringResource(R.plurals.render_tracks_count, playlist.tracks.size, playlist.tracks.size),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) } },
    )
}

/**
 * The searchable checklist behind "Choose tracks…". The ticks are kept by the Render tab, so a
 * later batch can reuse them; this dialog edits a copy and hands it back on Done.
 */
@Composable
internal fun TrackChecklistDialog(
    tracks: List<BatchTrack>,
    chosen: List<String>,
    onDone: (List<String>) -> Unit,
    onDismiss: () -> Unit,
) {
    var picked by remember { mutableStateOf(chosen) }
    var query by rememberSaveable { mutableStateOf("") }
    val shown = if (query.isBlank()) tracks else tracks.filter { it.title.contains(query.trim(), ignoreCase = true) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.render_scope_pick_tracks)) },
        text = {
            Column {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    label = { Text(stringResource(R.string.render_tracks_search)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                LazyColumn(Modifier.heightIn(max = 380.dp).padding(top = 8.dp)) {
                    items(shown, key = { it.uri }) { track ->
                        val checked = track.uri in picked
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .toggleable(value = checked, role = Role.Checkbox) { on ->
                                    picked = if (on) picked + track.uri else picked - track.uri
                                }.padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Checkbox(checked = checked, onCheckedChange = null)
                            Text(track.title, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onDone(picked) }) {
                Text(pluralStringResource(R.plurals.render_use_tracks, picked.size, picked.size))
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.render_cancel)) } },
    )
}
