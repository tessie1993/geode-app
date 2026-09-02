package dev.geode.ui.studio

import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.AnimatableParam
import dev.geode.editor.AnimatableParams
import dev.geode.editor.AutoCut
import dev.geode.editor.Clip
import dev.geode.editor.ClipContent
import dev.geode.editor.ClipId
import dev.geode.editor.EditError
import dev.geode.editor.EditResult
import dev.geode.editor.EditorProject
import dev.geode.editor.Keyframe
import dev.geode.editor.KeyframeId
import dev.geode.editor.KeyframeResult
import dev.geode.editor.KeyframeTrack
import dev.geode.editor.Lane
import dev.geode.editor.LaneId
import dev.geode.editor.LaneKind
import dev.geode.editor.Marker
import dev.geode.editor.MarkerId
import dev.geode.editor.OverlapPolicy
import dev.geode.editor.RippleScope
import dev.geode.editor.TapInSession
import dev.geode.editor.TapResult
import dev.geode.editor.Timeline
import dev.geode.ui.EditorUiState
import java.util.UUID

private enum class PickKind {
    VIDEO,
    STILL,
    OVERLAY,
    AUDIO,
}

/** The timeline: ruler, marker lane, one row per lane, keyframe rows for the selection, and the playhead. */
@Composable
fun TimelineEditor(
    state: EditorUiState,
    actions: EditorActions,
    onClose: () -> Unit,
) {
    val context = LocalContext.current
    val project = state.project
    var pxPerMs by rememberSaveable { mutableStateOf(TimelineScale.DEFAULT_PX_PER_MS) }
    var selectedClip by remember { mutableStateOf<ClipId?>(null) }
    var selectedMarker by remember { mutableStateOf<MarkerId?>(null) }
    var selectedKey by remember { mutableStateOf<KeyframeId?>(null) }
    var editError by remember { mutableStateOf<EditError?>(null) }
    var autoCutOpen by remember { mutableStateOf(false) }
    var textLane by remember { mutableStateOf<LaneId?>(null) }
    var tapSession by remember { mutableStateOf<TapInSession?>(null) }
    var picking by remember { mutableStateOf<Pair<LaneId, PickKind>?>(null) }
    var trackSheet by remember { mutableStateOf<ClipId?>(null) }
    var trackSheetOpen by remember { mutableStateOf(false) }
    val scale = TimelineScale(pxPerMs, maxOf(project.timeline.durationMs, MIN_CONTENT_MS) + CONTENT_MARGIN_MS)
    val laneNames = LANE_NAME_LABELS.associate { (kind, label) -> kind to stringResource(label) }

    fun applyResult(result: EditResult) {
        when (result) {
            is EditResult.Applied -> actions.edit { it.apply(result).fitted() }
            is EditResult.Rejected -> editError = result.error
        }
    }

    fun addClip(
        laneId: LaneId,
        content: ClipContent,
        durationMs: Long,
        sourceDurationMs: Long = 0L,
    ) {
        actions.edit { p ->
            val clip =
                Clip(actions.newClipId(), content, startMs = state.playheadMs, durationMs = durationMs, sourceDurationMs = sourceDurationMs)
            when (val result = p.timeline.addClip(laneId, clip)) {
                is EditResult.Applied -> p.apply(result).fitted()
                is EditResult.Rejected -> p.also { editError = result.error }
            }
        }
    }

    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            val target = picking ?: return@rememberLauncherForActivityResult
            picking = null
            if (uri == null) return@rememberLauncherForActivityResult
            runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            val text = uri.toString()
            when (target.second) {
                PickKind.VIDEO ->
                    actions.describeMedia(
                        uri,
                    ) { media -> addClip(target.first, ClipContent.Video(text), media.durationMs, media.durationMs) }
                PickKind.AUDIO ->
                    actions.describeMedia(
                        uri,
                    ) { media -> addClip(target.first, ClipContent.Audio(text), media.durationMs, media.durationMs) }
                PickKind.STILL -> addClip(target.first, ClipContent.Still(text), STILL_MS)
                PickKind.OVERLAY -> addClip(target.first, ClipContent.Overlay(text), OVERLAY_MS)
            }
        }

    fun pick(
        laneId: LaneId,
        kind: PickKind,
    ) {
        picking = laneId to kind
        picker.launch(
            when (kind) {
                PickKind.VIDEO -> arrayOf("video/*")
                PickKind.STILL, PickKind.OVERLAY -> arrayOf("image/*")
                PickKind.AUDIO -> arrayOf("audio/*")
            },
        )
    }

    fun addLane(kind: LaneKind) {
        actions.edit { p ->
            val count = p.timeline.lanes.count { it.kind == kind } + 1
            p.copy(
                timeline =
                    p.timeline.copy(
                        lanes =
                            p.timeline.lanes + Lane(LaneId(UUID.randomUUID().toString()), kind, "${laneNames[kind]} $count"),
                    ),
            )
        }
    }

    fun tap() {
        val session = tapSession ?: return
        val at = actions.playbackPositionMs() ?: state.playheadMs
        tapSession =
            when (val result = session.tap(actions.newMarkerId(), at)) {
                is TapResult.Placed -> result.session
                is TapResult.Debounced -> result.session
            }
    }

    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        EditorHeader(
            canUndo = state.history.canUndo,
            canRedo = state.history.canRedo,
            playheadMs = state.playheadMs,
            onUndo = actions::undo,
            onRedo = actions::redo,
            onZoom = { pxPerMs = (pxPerMs * it).coerceIn(TimelineScale.MIN_PX_PER_MS, TimelineScale.MAX_PX_PER_MS) },
            onClose = onClose,
        )
        EditorToolbar(
            tapSession = tapSession,
            onAddLane = ::addLane,
            onAddMarker = { actions.edit { p -> p.copy(markers = p.markers.add(Marker(actions.newMarkerId(), state.playheadMs))) } },
            onTapStart = { tapSession = TapInSession() },
            onTap = ::tap,
            onTapUndo = { tapSession = tapSession?.undoLast() },
            onTapDone = {
                val session = tapSession
                tapSession = null
                if (session != null && session.count > 0) actions.edit { p -> p.copy(markers = session.commitTo(p.markers)) }
            },
            onTapCancel = { tapSession = null },
            onAutoCut = { autoCutOpen = true },
        )
        val clip = selectedClip?.let(project.timeline::clip)
        SelectionToolbar(
            clipSelected = clip != null,
            clipEnabled = clip?.enabled ?: true,
            markerSelected = selectedMarker != null,
            keySelected = selectedKey != null,
            onSplit = { clip?.let { applyResult(project.timeline.splitClip(it.id, state.playheadMs, actions.newClipId())) } },
            onDelete = {
                clip?.let {
                    applyResult(project.timeline.deleteClip(it.id))
                    selectedClip = null
                }
            },
            onRippleDelete = {
                clip?.let {
                    applyResult(project.timeline.rippleDeleteClip(it.id, RippleScope.LANE))
                    selectedClip = null
                }
            },
            onDuplicate = { clip?.let { applyResult(project.timeline.duplicateClip(it.id, actions.newClipId())) } },
            onToggleEnabled = {
                clip?.let { c ->
                    actions.edit { p -> p.copy(timeline = p.timeline.withClip(c.copy(enabled = !c.enabled))) }
                }
            },
            onDeleteMarker = {
                selectedMarker?.let { id -> actions.edit { p -> p.copy(markers = p.markers.remove(id)) } }
                selectedMarker = null
            },
            onDeleteKey = {
                val id = selectedKey
                if (id != null) {
                    actions.edit { p ->
                        val track = p.keyframes.tracks.firstOrNull { t -> t.key(id) != null }
                        if (track == null) p else p.copy(keyframes = p.keyframes.withTrack(track.removeKey(id)))
                    }
                }
                selectedKey = null
            },
            onAnimateProgramme = {
                trackSheet = null
                trackSheetOpen = true
            },
            onAnimateClip = {
                trackSheet = selectedClip
                trackSheetOpen = true
            },
        )
        selectedKey?.let { id ->
            val track = project.keyframes.tracks.firstOrNull { t -> t.key(id) != null }
            val key = track?.key(id)
            if (track != null && key != null) {
                KeyEditor(key = key, param = AnimatableParams.find(track.paramId)) { changed ->
                    actions.edit { p -> p.withKeyOn(track, changed) }
                }
            }
        }
        editError?.let { error ->
            Text(editErrorMessage(error), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
        }
        if (project.timeline.lanes.isEmpty()) {
            Text(
                stringResource(R.string.editor_empty),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        val keyTracks = project.keyframes.tracksFor(null) + (selectedClip?.let(project.keyframes::tracksFor) ?: emptyList())
        Lanes(
            project = project,
            scale = scale,
            playheadMs = state.playheadMs,
            keyTracks = keyTracks,
            selectedClip = selectedClip,
            selectedMarker = selectedMarker,
            selectedKey = selectedKey,
            actions = actions,
            onSelectClip = {
                selectedClip = it
                editError = null
            },
            onSelectMarker = { selectedMarker = it },
            onSelectKey = { selectedKey = it },
            onResult = ::applyResult,
            onAddClip = { lane ->
                when (lane.kind) {
                    LaneKind.Visual -> addClip(lane.id, ClipContent.Scene(actions.currentSceneId()), SCENE_MS)
                    LaneKind.Media -> pick(lane.id, PickKind.VIDEO)
                    LaneKind.Text -> textLane = lane.id
                    LaneKind.Overlay -> pick(lane.id, PickKind.OVERLAY)
                    LaneKind.Audio -> pick(lane.id, PickKind.AUDIO)
                }
            },
            onAddStill = { lane -> pick(lane.id, PickKind.STILL) },
        )
    }

    if (trackSheetOpen) {
        val scopeClip = trackSheet?.let(project.timeline::clip)
        val available =
            when (scopeClip?.content) {
                null -> AnimatableParams.scene
                is ClipContent.Video -> AnimatableParams.clip
                is ClipContent.Scene -> AnimatableParams.scene
                else -> emptyList()
            }
        AddTrackSheet(
            params = available,
            onPick = { param ->
                actions.edit { p -> p.withNewTrack(param, scopeClip, state.playheadMs, actions) }
                trackSheetOpen = false
            },
            onDismiss = { trackSheetOpen = false },
        )
    }

    textLane?.let { laneId ->
        TextClipDialog(
            onConfirm = {
                addClip(laneId, ClipContent.Text(it), TEXT_MS)
                textLane = null
            },
            onDismiss = { textLane = null },
        )
    }
    if (autoCutOpen) {
        AutoCutSheet(
            envelopeFor = actions::transientEnvelope,
            onMarkers = { hits ->
                actions.edit { p -> p.copy(markers = p.markers.addAll(AutoCut.markersFrom(hits, idFor = { actions.newMarkerId() }))) }
                autoCutOpen = false
            },
            onClips = { hits, untilMs ->
                actions.edit { p -> p.cutVisualLane(hits, untilMs, actions, laneNames[LaneKind.Visual].orEmpty()) }
                autoCutOpen = false
            },
            onDismiss = { autoCutOpen = false },
        )
    }
}

/** A new track starts with one key holding the parameter's value now, so the curve has something to leave from. */
private fun EditorProject.withNewTrack(
    param: AnimatableParam,
    scopeClip: Clip?,
    atMs: Long,
    actions: EditorActions,
): EditorProject {
    val clipId = scopeClip?.id
    if (keyframes.track(param.id, clipId) != null) return this
    val value =
        when (val content = scopeClip?.content) {
            is ClipContent.Video -> param.readClip?.invoke(content.edit)
            else -> param.readScene?.invoke(actions.currentSceneParams())
        } ?: return this
    val track = KeyframeTrack(paramId = param.id, clipId = clipId)
    return withKeyOn(track, Keyframe(actions.newKeyframeId(), atMs, value))
}

/** The programme runs to the last clip; every applied edit re-fits it. */
private fun EditorProject.fitted(): EditorProject {
    val end = timeline.lanes.maxOfOrNull { lane -> lane.clips.maxOfOrNull { it.endMs } ?: 0L } ?: 0L
    return if (end == timeline.durationMs) this else copy(timeline = timeline.copy(durationMs = end))
}

internal fun Timeline.withLane(lane: Lane): Timeline = copy(lanes = lanes.map { if (it.id == lane.id) lane else it })

private fun Timeline.withClip(clip: Clip): Timeline {
    val lane = laneOf(clip.id) ?: return this
    return withLane(lane.withClips(lane.clips.map { if (it.id == clip.id) clip else it }))
}

internal fun EditorProject.withKeyOn(
    track: KeyframeTrack,
    key: Keyframe,
): EditorProject =
    when (val result = track.withKey(key)) {
        is KeyframeResult.Applied -> copy(keyframes = keyframes.withTrack(result.track))
        is KeyframeResult.Rejected -> this
    }

/** Auto-cut clips land on the first visual lane, overwriting what was there; a lane is made if none exists. */
private fun EditorProject.cutVisualLane(
    hits: List<dev.geode.editor.TransientHit>,
    untilMs: Long,
    actions: EditorActions,
    laneLabel: String,
): EditorProject {
    val existing = timeline.lanes.firstOrNull { it.kind == LaneKind.Visual }
    val lane = existing ?: Lane(LaneId(UUID.randomUUID().toString()), LaneKind.Visual, "$laneLabel 1")
    val base = if (existing == null) copy(timeline = timeline.copy(lanes = timeline.lanes + lane)) else this
    val sceneId = actions.currentSceneId()
    val clips = AutoCut.clipsFrom(hits, untilMs, { actions.newClipId() }) { _, _, _ -> ClipContent.Scene(sceneId) }
    return clips
        .fold(base) { project, clip ->
            when (val result = project.timeline.addClip(lane.id, clip, OverlapPolicy.OVERWRITE)) {
                is EditResult.Applied -> project.apply(result)
                is EditResult.Rejected -> project
            }
        }.fitted()
}

private val LANE_NAME_LABELS: List<Pair<LaneKind, Int>> =
    listOf(LaneKind.Visual, LaneKind.Media, LaneKind.Text, LaneKind.Overlay, LaneKind.Audio).map { it to laneKindLabel(it) }

private const val MIN_CONTENT_MS = 60_000L
private const val CONTENT_MARGIN_MS = 15_000L
private const val SCENE_MS = 4_000L
private const val TEXT_MS = 3_000L
private const val STILL_MS = 3_000L
private const val OVERLAY_MS = 4_000L
