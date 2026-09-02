package dev.geode.ui.studio

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.EditError
import dev.geode.editor.LaneKind
import dev.geode.editor.TapInSession
import dev.geode.ui.CrystalButton

/** Back, title, undo/redo and zoom. */
@Composable
fun EditorHeader(
    canUndo: Boolean,
    canRedo: Boolean,
    playheadMs: Long,
    exporting: Boolean,
    onUndo: () -> Unit,
    onRedo: () -> Unit,
    onZoom: (Float) -> Unit,
    onExport: () -> Unit,
    onClose: () -> Unit,
) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        TextButton(onClick = onClose) { Text(stringResource(R.string.action_back)) }
        Text(
            stringResource(R.string.editor_playhead, clockLabel(playheadMs)),
            style = MaterialTheme.typography.labelMedium,
            modifier = Modifier.weight(1f),
        )
        CrystalButton(compact = true, filled = false, enabled = canUndo, onClick = onUndo) { Text(stringResource(R.string.editor_undo)) }
        CrystalButton(compact = true, filled = false, enabled = canRedo, onClick = onRedo) { Text(stringResource(R.string.editor_redo)) }
        CrystalButton(
            compact = true,
            filled = false,
            onClick = { onZoom(1f / ZOOM_STEP) },
        ) { Text(stringResource(R.string.editor_zoom_out)) }
        CrystalButton(compact = true, filled = false, onClick = { onZoom(ZOOM_STEP) }) { Text(stringResource(R.string.editor_zoom_in)) }
        CrystalButton(compact = true, enabled = !exporting, onClick = onExport) { Text(stringResource(R.string.editor_export)) }
    }
}

/** Lane creation, markers and auto-cut. */
@Composable
fun EditorToolbar(
    tapSession: TapInSession?,
    onAddLane: (LaneKind) -> Unit,
    onAddMarker: () -> Unit,
    onTapStart: () -> Unit,
    onTap: () -> Unit,
    onTapUndo: () -> Unit,
    onTapDone: () -> Unit,
    onTapCancel: () -> Unit,
    onAutoCut: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        if (tapSession == null) {
            LANE_KINDS.forEach { (kind, label) ->
                CrystalButton(compact = true, filled = false, onClick = { onAddLane(kind) }) {
                    Text(stringResource(R.string.editor_add_lane, stringResource(label)))
                }
            }
            CrystalButton(compact = true, filled = false, onClick = onAddMarker) { Text(stringResource(R.string.editor_add_marker)) }
            CrystalButton(compact = true, filled = false, onClick = onTapStart) { Text(stringResource(R.string.editor_tap_in)) }
            CrystalButton(compact = true, filled = false, onClick = onAutoCut) { Text(stringResource(R.string.editor_auto_cut)) }
        } else {
            CrystalButton(compact = true, onClick = onTap) { Text(stringResource(R.string.editor_tap)) }
            Text(
                stringResource(R.string.editor_tap_count, tapSession.count),
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.align(Alignment.CenterVertically),
            )
            CrystalButton(compact = true, filled = false, enabled = tapSession.count > 0, onClick = onTapUndo) {
                Text(stringResource(R.string.editor_tap_undo))
            }
            CrystalButton(compact = true, filled = false, onClick = onTapDone) { Text(stringResource(R.string.editor_tap_done)) }
            CrystalButton(compact = true, filled = false, onClick = onTapCancel) { Text(stringResource(R.string.action_cancel)) }
        }
    }
}

/** What can be done to the selected clip or marker. */
@Composable
fun SelectionToolbar(
    clipSelected: Boolean,
    clipEnabled: Boolean,
    markerSelected: Boolean,
    keySelected: Boolean,
    canTransition: Boolean,
    onTransition: () -> Unit,
    onSplit: () -> Unit,
    onDelete: () -> Unit,
    onRippleDelete: () -> Unit,
    onDuplicate: () -> Unit,
    onToggleEnabled: () -> Unit,
    onDeleteMarker: () -> Unit,
    onDeleteKey: () -> Unit,
    onAnimateProgramme: () -> Unit,
    onAnimateClip: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        CrystalButton(compact = true, filled = false, onClick = onAnimateProgramme) { Text(stringResource(R.string.curve_animate_scene)) }
        if (clipSelected) {
            CrystalButton(compact = true, filled = false, onClick = onAnimateClip) { Text(stringResource(R.string.curve_animate_clip)) }
            if (canTransition) {
                CrystalButton(compact = true, filled = false, onClick = onTransition) { Text(stringResource(R.string.editor_transition_ellipsis)) }
            }
            CrystalButton(compact = true, filled = false, onClick = onSplit) { Text(stringResource(R.string.editor_split)) }
            CrystalButton(compact = true, filled = false, onClick = onDelete) { Text(stringResource(R.string.editor_delete)) }
            CrystalButton(compact = true, filled = false, onClick = onRippleDelete) { Text(stringResource(R.string.editor_ripple_delete)) }
            CrystalButton(compact = true, filled = false, onClick = onDuplicate) { Text(stringResource(R.string.editor_duplicate)) }
            CrystalButton(compact = true, filled = false, onClick = onToggleEnabled) {
                Text(stringResource(if (clipEnabled) R.string.editor_disable else R.string.editor_enable))
            }
        }
        if (markerSelected) {
            CrystalButton(compact = true, filled = false, onClick = onDeleteMarker) { Text(stringResource(R.string.editor_delete_marker)) }
        }
        if (keySelected) {
            CrystalButton(compact = true, filled = false, onClick = onDeleteKey) { Text(stringResource(R.string.editor_delete_key)) }
        }
    }
}

@Composable
fun TextClipDialog(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var text by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.editor_text_title)) },
        text = {
            Column {
                OutlinedTextField(value = text, onValueChange = { text = it }, minLines = 2, modifier = Modifier.fillMaxWidth())
            }
        },
        confirmButton = {
            TextButton(enabled = text.isNotBlank(), onClick = { onConfirm(text.trim()) }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
fun editErrorMessage(error: EditError): String =
    when (error) {
        is EditError.LaneNotFound, is EditError.ClipNotFound -> stringResource(R.string.editor_err_not_found)
        is EditError.LaneLocked -> stringResource(R.string.editor_err_locked)
        is EditError.WrongLaneKind -> stringResource(R.string.editor_err_wrong_lane)
        is EditError.Overlaps -> stringResource(R.string.editor_err_overlaps)
        is EditError.NeedsSplit -> stringResource(R.string.editor_err_needs_split)
        EditError.TooShort -> stringResource(R.string.editor_err_too_short)
        EditError.OutsideClip -> stringResource(R.string.editor_err_outside_clip)
    }

fun laneKindLabel(kind: LaneKind): Int =
    when (kind) {
        LaneKind.Visual -> R.string.editor_lane_visual
        LaneKind.Media -> R.string.editor_lane_media
        LaneKind.Text -> R.string.editor_lane_text
        LaneKind.Overlay -> R.string.editor_lane_overlay
        LaneKind.Audio -> R.string.editor_lane_audio
    }

private val LANE_KINDS: List<Pair<LaneKind, Int>> =
    listOf(LaneKind.Visual, LaneKind.Media, LaneKind.Text, LaneKind.Overlay, LaneKind.Audio).map { it to laneKindLabel(it) }

private const val ZOOM_STEP = 1.5f
