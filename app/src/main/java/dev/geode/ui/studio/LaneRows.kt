package dev.geode.ui.studio

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorProject
import dev.geode.editor.Keyframe
import dev.geode.editor.KeyframeId
import dev.geode.editor.KeyframeResult
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.Lane
import dev.geode.editor.LaneKind
import dev.geode.editor.Marker
import dev.geode.editor.MarkerId

/** Header column beside the scrolling content column; every row height is fixed so the two stay aligned. */
@Composable
internal fun Lanes(
    project: EditorProject,
    scale: TimelineScale,
    playheadMs: Long,
    keyTracks: List<KeyframeTrack>,
    selectedClip: ClipId?,
    selectedMarker: MarkerId?,
    selectedKey: KeyframeId?,
    actions: EditorActions,
    onSelectClip: (ClipId?) -> Unit,
    onSelectMarker: (MarkerId?) -> Unit,
    onSelectKey: (KeyframeId?) -> Unit,
    onResult: (EditResult) -> Unit,
    onAddClip: (Lane) -> Unit,
    onAddStill: (Lane) -> Unit,
) {
    val horizontal = rememberScrollState()
    val vertical = rememberScrollState()
    Row(Modifier.fillMaxWidth().verticalScroll(vertical)) {
        Column(Modifier.width(LANE_HEADER_WIDTH)) {
            Spacer(Modifier.height(RULER_HEIGHT))
            Box(Modifier.height(MARKER_LANE_HEIGHT), contentAlignment = Alignment.CenterStart) {
                Text(stringResource(R.string.editor_markers), style = MaterialTheme.typography.labelSmall)
            }
            project.timeline.lanes.forEach { lane ->
                LaneHeader(lane, actions, onAddClip = { onAddClip(lane) }, onAddStill = { onAddStill(lane) })
            }
            keyTracks.forEach { track ->
                Box(Modifier.height(KEYFRAME_LANE_HEIGHT), contentAlignment = Alignment.CenterStart) {
                    Text(track.paramId.value, style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
        }
        Box(Modifier.weight(1f).horizontalScroll(horizontal)) {
            Column {
                TimeRuler(scale, onScrub = actions::setPlayhead)
                LaneContentBox(scale, MARKER_LANE_HEIGHT) {
                    MarkerLane(
                        markers = project.markers,
                        scale = scale,
                        selected = selectedMarker,
                        snapContext = project.snapContext(playheadMs),
                        onSelect = onSelectMarker,
                        onAdd = { ms -> actions.edit { p -> p.copy(markers = p.markers.add(Marker(actions.newMarkerId(), ms))) } },
                        onMove = {
                            id,
                            ms,
                            snap,
                            context,
                            ->
                            actions.edit { p -> p.copy(markers = p.markers.moveTo(id, ms, snap, context)) }
                        },
                        modifier = Modifier.fillMaxSize(),
                    )
                }
                project.timeline.lanes.forEach { lane ->
                    LaneContentBox(scale, LANE_HEIGHT) {
                        ClipStrip(
                            timeline = project.timeline,
                            lane = lane,
                            scale = scale,
                            selected = selectedClip,
                            snapContext = { project.snapContext(playheadMs, excludeClip = it) },
                            onSelect = onSelectClip,
                            onSplit = { id, ms -> onResult(project.timeline.splitClip(id, ms, actions.newClipId())) },
                            onResult = onResult,
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                }
                keyTracks.forEach { track ->
                    LaneContentBox(scale, KEYFRAME_LANE_HEIGHT) {
                        KeyframeLane(
                            track = track,
                            scale = scale,
                            selected = selectedKey,
                            snapContext = project.snapContext(playheadMs),
                            onSelect = onSelectKey,
                            onAdd = { ms ->
                                actions.edit { p ->
                                    p.withKeyOn(
                                        track,
                                        Keyframe(
                                            actions.newKeyframeId(),
                                            ms,
                                            track.valueAt(ms) ?: return@edit p,
                                        ),
                                    )
                                }
                            },
                            onMove = { id, ms, snap, context ->
                                actions.edit { p ->
                                    when (val moved = track.moveKey(id, ms, snap, context)) {
                                        is KeyframeResult.Applied -> p.copy(keyframes = p.keyframes.withTrack(moved.track))
                                        is KeyframeResult.Rejected -> p
                                    }
                                }
                            },
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                }
            }
            Playhead(scale, playheadMs, Modifier.matchParentSize())
        }
    }
}

@Composable
private fun LaneHeader(
    lane: Lane,
    actions: EditorActions,
    onAddClip: () -> Unit,
    onAddStill: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    Column(Modifier.height(LANE_HEIGHT), verticalArrangement = Arrangement.Center) {
        Text(lane.name, style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            HeaderToggle(stringResource(R.string.editor_mute_short), lane.muted) {
                actions.edit { p -> p.copy(timeline = p.timeline.withLane(lane.copy(muted = !lane.muted))) }
            }
            HeaderToggle(stringResource(R.string.editor_lock_short), lane.locked) {
                actions.edit { p -> p.copy(timeline = p.timeline.withLane(lane.copy(locked = !lane.locked))) }
            }
            Box {
                HeaderToggle(stringResource(R.string.editor_add_short), false) {
                    if (lane.kind == LaneKind.Media) menu = true else onAddClip()
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.editor_add_video)) },
                        onClick = {
                            menu = false
                            onAddClip()
                        },
                    )
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.editor_add_still)) },
                        onClick = {
                            menu = false
                            onAddStill()
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun HeaderToggle(
    label: String,
    on: Boolean,
    onClick: () -> Unit,
) {
    Text(
        label,
        style = MaterialTheme.typography.labelSmall,
        color = if (on) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.clickable(onClick = onClick),
    )
}
