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
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassTextField
import dev.geode.ui.glass.glassSurface

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
        GlassButton(text = stringResource(R.string.action_back), onClick = onClose)
        Text(
            stringResource(R.string.editor_playhead, clockLabel(playheadMs)),
            style = MaterialTheme.typography.labelMedium,
            color = GlassPalette.textSecondary,
            modifier = Modifier.padding(horizontal = 8.dp),
        )
        GlassButton(text = stringResource(R.string.editor_undo), enabled = canUndo, onClick = onUndo)
        GlassButton(text = stringResource(R.string.editor_redo), enabled = canRedo, onClick = onRedo)
        GlassButton(text = stringResource(R.string.editor_zoom_out), onClick = { onZoom(1f / ZOOM_STEP) })
        GlassButton(text = stringResource(R.string.editor_zoom_in), onClick = { onZoom(ZOOM_STEP) })
        GlassButton(
            text = stringResource(R.string.editor_export),
            enabled = !exporting,
            tint = GlassPalette.mint,
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
                GlassButton(text = stringResource(R.string.editor_add_lane, stringResource(label)), onClick = { onAddLane(kind) })
            }
            GlassButton(text = stringResource(R.string.editor_add_marker), onClick = onAddMarker)
            GlassButton(text = stringResource(R.string.editor_tap_in), onClick = onTapStart)
            GlassButton(text = stringResource(R.string.editor_auto_cut), onClick = onAutoCut)
            if (hasLyrics) {
                GlassButton(text = stringResource(R.string.editor_lyric_captions), onClick = onLyricCaptions)
            }
            GlassButton(text = stringResource(R.string.editor_import_srt), onClick = onImportSrt)
            GlassButton(text = stringResource(R.string.editor_export_srt), onClick = onExportSrt)
            GlassButton(text = stringResource(R.string.editor_export_chapters), onClick = onExportChapters)
        } else {
            GlassButton(text = stringResource(R.string.editor_tap), tint = GlassPalette.mint, onClick = onTap)
            Text(
                stringResource(R.string.editor_tap_count, tapSession.count),
                style = MaterialTheme.typography.labelMedium,
                color = GlassPalette.textSecondary,
                modifier = Modifier.align(Alignment.CenterVertically),
            )
            GlassButton(text = stringResource(R.string.editor_tap_undo), enabled = tapSession.count > 0, onClick = onTapUndo)
            GlassButton(text = stringResource(R.string.editor_tap_done), onClick = onTapDone)
            GlassButton(text = stringResource(R.string.action_cancel), onClick = onTapCancel)
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
        GlassButton(text = stringResource(R.string.curve_animate_scene), onClick = onAnimateProgramme)
        if (clipSelected) {
            GlassButton(text = stringResource(R.string.curve_animate_clip), onClick = onAnimateClip)
            if (canTransition) {
                GlassButton(text = stringResource(R.string.editor_transition_ellipsis), onClick = onTransition)
            }
            GlassButton(text = stringResource(R.string.editor_split), onClick = onSplit)
            GlassButton(text = stringResource(R.string.editor_delete), tint = GlassPalette.pink, onClick = onDelete)
            GlassButton(text = stringResource(R.string.editor_ripple_delete), tint = GlassPalette.pink, onClick = onRippleDelete)
            GlassButton(text = stringResource(R.string.editor_duplicate), onClick = onDuplicate)
            GlassButton(
                text = stringResource(if (clipEnabled) R.string.editor_disable else R.string.editor_enable),
                onClick = onToggleEnabled,
            )
        }
        if (markerSelected) {
            GlassButton(text = stringResource(R.string.editor_delete_marker), tint = GlassPalette.pink, onClick = onDeleteMarker)
        }
        if (keySelected) {
            GlassButton(text = stringResource(R.string.editor_delete_key), tint = GlassPalette.pink, onClick = onDeleteKey)
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
                .glassSurface(shape = GlassShapes.tile)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.editor_text_title), style = MaterialTheme.typography.titleLarge, color = GlassPalette.textPrimary)
            GlassTextField(value = text, onValueChange = { text = it }, modifier = Modifier.fillMaxWidth())
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                GlassButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
                GlassButton(
                    text = stringResource(R.string.action_save),
                    enabled = text.isNotBlank(),
                    tint = GlassPalette.mint,
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
