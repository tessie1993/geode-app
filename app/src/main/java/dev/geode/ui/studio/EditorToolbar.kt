package dev.geode.ui.studio

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.window.Dialog
import dev.geode.R
import dev.geode.editor.EditError
import dev.geode.editor.LaneKind
import dev.geode.editor.TapInSession
import dev.geode.ui.opaline.creative.CreativeButton
import dev.geode.ui.opaline.creative.CreativeColors
import dev.geode.ui.opaline.creative.CreativeShapes
import dev.geode.ui.opaline.creative.CreativeTextField
import dev.geode.ui.opaline.creative.creativeSurface

/** Back, title, undo/redo and zoom, as a row of bubble buttons. */
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
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        CreativeButton(text = stringResource(R.string.action_back), onClick = onClose)
        Text(
            stringResource(R.string.editor_playhead, clockLabel(playheadMs)),
            style = MaterialTheme.typography.labelMedium,
            color = CreativeColors.textSecondary,
            modifier = Modifier.padding(horizontal = 8.dp),
        )
        CreativeButton(text = stringResource(R.string.editor_undo), enabled = canUndo, onClick = onUndo)
        CreativeButton(text = stringResource(R.string.editor_redo), enabled = canRedo, onClick = onRedo)
        CreativeButton(text = stringResource(R.string.editor_zoom_out), onClick = { onZoom(1f / ZOOM_STEP) })
        CreativeButton(text = stringResource(R.string.editor_zoom_in), onClick = { onZoom(ZOOM_STEP) })
        CreativeButton(
            text = stringResource(R.string.editor_export),
            enabled = !exporting,
            tint = CreativeColors.mint,
            onClick = onExport,
        )
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
    hasLyrics: Boolean,
    onLyricCaptions: () -> Unit,
    onImportSrt: () -> Unit,
    onExportSrt: () -> Unit,
    onExportChapters: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        if (tapSession == null) {
            LANE_KINDS.forEach { (kind, label) ->
                CreativeButton(text = stringResource(R.string.editor_add_lane, stringResource(label)), onClick = { onAddLane(kind) })
            }
            CreativeButton(text = stringResource(R.string.editor_add_marker), onClick = onAddMarker)
            CreativeButton(text = stringResource(R.string.editor_tap_in), onClick = onTapStart)
            CreativeButton(text = stringResource(R.string.editor_auto_cut), onClick = onAutoCut)
            if (hasLyrics) {
                CreativeButton(text = stringResource(R.string.editor_lyric_captions), onClick = onLyricCaptions)
            }
            CreativeButton(text = stringResource(R.string.editor_import_srt), onClick = onImportSrt)
            CreativeButton(text = stringResource(R.string.editor_export_srt), onClick = onExportSrt)
            CreativeButton(text = stringResource(R.string.editor_export_chapters), onClick = onExportChapters)
        } else {
            CreativeButton(text = stringResource(R.string.editor_tap), tint = CreativeColors.mint, onClick = onTap)
            Text(
                stringResource(R.string.editor_tap_count, tapSession.count),
                style = MaterialTheme.typography.labelMedium,
                color = CreativeColors.textSecondary,
                modifier = Modifier.align(Alignment.CenterVertically),
            )
            CreativeButton(text = stringResource(R.string.editor_tap_undo), enabled = tapSession.count > 0, onClick = onTapUndo)
            CreativeButton(text = stringResource(R.string.editor_tap_done), onClick = onTapDone)
            CreativeButton(text = stringResource(R.string.action_cancel), onClick = onTapCancel)
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
        CreativeButton(text = stringResource(R.string.curve_animate_scene), onClick = onAnimateProgramme)
        if (clipSelected) {
            CreativeButton(text = stringResource(R.string.curve_animate_clip), onClick = onAnimateClip)
            if (canTransition) {
                CreativeButton(text = stringResource(R.string.editor_transition_ellipsis), onClick = onTransition)
            }
            CreativeButton(text = stringResource(R.string.editor_split), onClick = onSplit)
            CreativeButton(text = stringResource(R.string.editor_delete), tint = CreativeColors.pink, onClick = onDelete)
            CreativeButton(text = stringResource(R.string.editor_ripple_delete), tint = CreativeColors.pink, onClick = onRippleDelete)
            CreativeButton(text = stringResource(R.string.editor_duplicate), onClick = onDuplicate)
            CreativeButton(
                text = stringResource(if (clipEnabled) R.string.editor_disable else R.string.editor_enable),
                onClick = onToggleEnabled,
            )
        }
        if (markerSelected) {
            CreativeButton(text = stringResource(R.string.editor_delete_marker), tint = CreativeColors.pink, onClick = onDeleteMarker)
        }
        if (keySelected) {
            CreativeButton(text = stringResource(R.string.editor_delete_key), tint = CreativeColors.pink, onClick = onDeleteKey)
        }
    }
}

@Composable
fun TextClipDialog(
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var text by remember { mutableStateOf("") }
    Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .creativeSurface(shape = CreativeShapes.tile)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                stringResource(R.string.editor_text_title),
                style = MaterialTheme.typography.titleLarge,
                color = CreativeColors.textPrimary,
            )
            CreativeTextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxWidth())
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                CreativeButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
                CreativeButton(
                    text = stringResource(R.string.action_save),
                    enabled = text.isNotBlank(),
                    tint = CreativeColors.mint,
                    modifier = Modifier.padding(start = 8.dp),
                    onClick = { onConfirm(text.trim()) },
                )
            }
        }
    }
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
