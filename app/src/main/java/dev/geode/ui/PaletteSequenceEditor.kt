package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.PaletteStore

/**
 * The palette sequence: saved palettes in play order, one step to the next every 4, 8, 16 or 32 beats with a
 * crossfade. The beats are the song's (core/sound Rhythm, so 16 and 32 land on phrase boundaries), a free
 * 120 BPM clock while there is no beat. On, it paints every shader style in place of the style's own palette.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PaletteSequenceEditor(palettes: SavedPalettes) {
    val sequence = palettes.sequence
    var adding by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(stringResource(R.string.palette_sequence_heading), style = MaterialTheme.typography.titleSmall)
        Text(stringResource(R.string.palette_sequence_explainer), style = MaterialTheme.typography.labelSmall)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.palette_sequence_on), modifier = Modifier.weight(1f))
            Switch(checked = sequence.on, onCheckedChange = { palettes.updateSequence(sequence.copy(on = it)) })
        }
        if (sequence.on) {
            Text(stringResource(R.string.palette_sequence_overrides), style = MaterialTheme.typography.labelSmall)
        }
        Text(stringResource(R.string.palette_sequence_beats), style = MaterialTheme.typography.labelSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            PaletteStore.BEATS.forEach { beats ->
                FilterChip(
                    selected = sequence.beats == beats,
                    onClick = { palettes.updateSequence(sequence.copy(beats = beats)) },
                    label = { Text(stringResource(R.string.palette_sequence_beats_n, beats), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        if (sequence.ids.isEmpty()) {
            Text(stringResource(R.string.palette_sequence_empty), style = MaterialTheme.typography.bodySmall)
        }
        sequence.ids.forEachIndexed { index, id ->
            val palette = palettes.items.firstOrNull { it.id == id }
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("${index + 1}", style = MaterialTheme.typography.labelMedium, modifier = Modifier.width(20.dp))
                Text(palette?.name ?: id, style = MaterialTheme.typography.labelMedium, modifier = Modifier.weight(1f))
                GradientPreview(palette?.stops.orEmpty(), modifier = Modifier.width(72.dp))
                IconButton(onClick = { palettes.updateSequence(sequence.copy(ids = sequence.ids.moved(index, -1))) }, enabled = index > 0) {
                    Icon(Icons.Filled.KeyboardArrowUp, contentDescription = stringResource(R.string.palette_move_up))
                }
                IconButton(
                    onClick = { palettes.updateSequence(sequence.copy(ids = sequence.ids.moved(index, 1))) },
                    enabled = index < sequence.ids.lastIndex,
                ) {
                    Icon(Icons.Filled.KeyboardArrowDown, contentDescription = stringResource(R.string.palette_move_down))
                }
                IconButton(onClick = { palettes.updateSequence(sequence.copy(ids = sequence.ids.filterIndexed { i, _ -> i != index })) }) {
                    Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.palette_remove))
                }
            }
        }
        Box {
            OutlinedButton(onClick = { adding = true }, enabled = sequence.ids.size < PaletteStore.MAX_SEQUENCE) {
                Text(stringResource(R.string.palette_sequence_add))
            }
            DropdownMenu(expanded = adding, onDismissRequest = { adding = false }) {
                palettes.items.forEach { palette ->
                    DropdownMenuItem(
                        text = { Text(palette.name) },
                        onClick = {
                            adding = false
                            palettes.updateSequence(sequence.copy(ids = sequence.ids + palette.id))
                        },
                    )
                }
            }
        }
    }
}

private fun List<String>.moved(
    index: Int,
    by: Int,
): List<String> {
    val to = (index + by).coerceIn(0, lastIndex)
    return toMutableList().apply { add(to, removeAt(index)) }
}
