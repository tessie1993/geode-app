package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.RuleField
import dev.geode.data.RuleOp
import dev.geode.data.SmartPlaylist
import dev.geode.data.SmartRule
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

/** The smart playlists under the hand-made ones: each row plays its current matches; the editor writes rules. */
@Composable
internal fun SmartPlaylistsSection(viewModel: LibraryViewModel) {
    val library by viewModel.library.collectAsStateWithLifecycle()
    var editing by remember { mutableStateOf<SmartPlaylist?>(null) }
    var creating by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(horizontal = 16.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.smart_playlists), style = MaterialTheme.typography.labelMedium, modifier = Modifier.weight(1f))
            CrystalButton(compact = true, filled = false, onClick = { creating = true }) { Text(stringResource(R.string.smart_new)) }
        }
        library.smartPlaylists.forEach { pl ->
            val count = remember(pl, library) { viewModel.resolveSmartPlaylist(pl).size }
            Row(
                Modifier
                    .fillMaxWidth()
                    .clickable { editing = pl }
                    .padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(pl.name)
                    Text(pluralStringResource(R.plurals.track_count, count, count), style = MaterialTheme.typography.bodySmall)
                }
                IconButton(onClick = { viewModel.playSmartPlaylist(pl) }) {
                    StoneIconArt(StoneIcon.PLAY, stringResource(R.string.action_play))
                }
                IconButton(onClick = { viewModel.deleteSmartPlaylist(pl.name) }) {
                    StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.action_delete))
                }
            }
        }
    }
    if (creating || editing != null) {
        SmartPlaylistDialog(
            initial = editing ?: SmartPlaylist(""),
            taken =
                library.smartPlaylists
                    .map { it.name }
                    .filterNot { it == editing?.name }
                    .toSet(),
            onSave = {
                editing?.name?.takeIf { old -> old != it.name }?.let(viewModel::deleteSmartPlaylist)
                viewModel.saveSmartPlaylist(it)
                creating = false
                editing = null
            },
            onDismiss = {
                creating = false
                editing = null
            },
        )
    }
}

@Composable
private fun SmartPlaylistDialog(
    initial: SmartPlaylist,
    taken: Set<String>,
    onSave: (SmartPlaylist) -> Unit,
    onDismiss: () -> Unit,
) {
    var draft by remember { mutableStateOf(initial) }
    val nameOk = draft.name.isNotBlank() && draft.name.trim() !in taken
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(if (initial.name.isBlank()) R.string.smart_new else R.string.smart_edit)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = draft.name,
                    onValueChange = { draft = draft.copy(name = it) },
                    singleLine = true,
                    label = { Text(stringResource(R.string.studio_rename_field)) },
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    FilterChip(
                        selected = draft.matchAll,
                        onClick = { draft = draft.copy(matchAll = true) },
                        label = { Text(stringResource(R.string.smart_match_all), style = MaterialTheme.typography.labelSmall) },
                    )
                    FilterChip(
                        selected = !draft.matchAll,
                        onClick = { draft = draft.copy(matchAll = false) },
                        label = { Text(stringResource(R.string.smart_match_any), style = MaterialTheme.typography.labelSmall) },
                    )
                }
                draft.rules.forEachIndexed { index, rule ->
                    RuleRow(
                        rule = rule,
                        onChange = { changed ->
                            draft = draft.copy(rules = draft.rules.mapIndexed { i, r -> if (i == index) changed else r })
                        },
                        onRemove = { draft = draft.copy(rules = draft.rules.filterIndexed { i, _ -> i != index }) },
                    )
                }
                TextButton(onClick = { draft = draft.copy(rules = draft.rules + SmartRule(RuleField.ARTIST, RuleOp.CONTAINS, "")) }) {
                    Text(stringResource(R.string.smart_add_rule))
                }
                OutlinedTextField(
                    value = if (draft.limit > 0) draft.limit.toString() else "",
                    onValueChange = { draft = draft.copy(limit = it.filter(Char::isDigit).toIntOrNull() ?: 0) },
                    singleLine = true,
                    label = { Text(stringResource(R.string.smart_limit)) },
                )
            }
        },
        confirmButton = {
            CrystalButton(enabled = nameOk, onClick = { onSave(draft.copy(name = draft.name.trim())) }) {
                Text(stringResource(R.string.action_save))
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
private fun RuleRow(
    rule: SmartRule,
    onChange: (SmartRule) -> Unit,
    onRemove: () -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            RuleField.entries.forEach { field ->
                FilterChip(
                    selected = rule.field == field,
                    onClick = { onChange(rule.copy(field = field, op = defaultOp(field))) },
                    label = { Text(stringResource(fieldLabel(field)), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            opsFor(rule.field).forEach { op ->
                FilterChip(
                    selected = rule.op == op,
                    onClick = { onChange(rule.copy(op = op)) },
                    label = { Text(stringResource(opLabel(op)), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (!rule.field.isFlag) {
                OutlinedTextField(
                    value = rule.value,
                    onValueChange = { onChange(rule.copy(value = it)) },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
            }
            TextButton(onClick = onRemove) { Text(stringResource(R.string.action_delete)) }
        }
    }
}

private fun defaultOp(field: RuleField): RuleOp =
    when {
        field.isText -> RuleOp.CONTAINS
        field.isFlag -> RuleOp.IS
        else -> RuleOp.AT_LEAST
    }

private fun opsFor(field: RuleField): List<RuleOp> =
    when {
        field.isFlag -> listOf(RuleOp.IS, RuleOp.IS_NOT)
        field.isText -> RuleOp.entries.filter { it.forText }
        else -> RuleOp.entries.filter { it.forNumber }
    }

private fun fieldLabel(field: RuleField): Int =
    when (field) {
        RuleField.TITLE -> R.string.library_sort_title
        RuleField.ARTIST -> R.string.library_sort_artist
        RuleField.ALBUM -> R.string.library_sort_album
        RuleField.FOLDER -> R.string.library_tab_folders
        RuleField.DURATION_MINUTES -> R.string.smart_field_minutes
        RuleField.ADDED_DAYS_AGO -> R.string.smart_field_added_days
        RuleField.PLAY_COUNT -> R.string.smart_field_plays
        RuleField.FAVOURITE -> R.string.auto_favourites
    }

private fun opLabel(op: RuleOp): Int =
    when (op) {
        RuleOp.CONTAINS -> R.string.smart_op_contains
        RuleOp.IS -> R.string.smart_op_is
        RuleOp.IS_NOT -> R.string.smart_op_is_not
        RuleOp.AT_LEAST -> R.string.smart_op_at_least
        RuleOp.AT_MOST -> R.string.smart_op_at_most
    }
