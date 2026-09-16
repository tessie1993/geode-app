package dev.geode.ui

import android.content.ClipData
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.export.ClipEdit
import dev.geode.export.ClipLook
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.export.StudioClip
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassLinearProgress
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassSlider
import dev.geode.ui.glass.GlassTextField
import dev.geode.ui.glass.GlassTile
import dev.geode.ui.glass.GlassToggle
import dev.geode.ui.glass.GlassTopBar
import dev.geode.ui.glass.floatOnWater
import dev.geode.ui.glass.glassSurface
import dev.geode.ui.studio.EditorActions
import dev.geode.ui.studio.TimelineEditor
import kotlin.math.roundToInt

@Composable
fun StudioRoute(viewModel: StudioViewModel = geodeViewModel()) {
    val studio by viewModel.studio.collectAsStateWithLifecycle()
    val editor by viewModel.editor.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { viewModel.refreshStudioClips() }
    StudioScreen(
        state = studio,
        editor = editor,
        editorActions = viewModel,
        onDescribe = viewModel::describeStudioClip,
        onRename = viewModel::renameStudioClip,
        onDelete = viewModel::deleteStudioClip,
        onExport = { clip, edit -> viewModel.startStudioExport(clip, edit) },
        onExportToDestination = { clip, edit, destination -> viewModel.startStudioExport(clip, edit, destination) },
        onCancelExport = viewModel::cancelStudioExport,
        onClearResult = viewModel::clearStudioResult,
    )
}

@Composable
internal fun StudioScreen(
    state: StudioUiState,
    editor: EditorUiState,
    editorActions: EditorActions,
    onDescribe: (Uri, (StudioClip) -> Unit) -> Unit,
    onRename: (String, String, (Boolean) -> Unit) -> Unit,
    onDelete: (String, (Boolean) -> Unit) -> Unit,
    onExport: (StudioClip, ClipEdit) -> Unit,
    onExportToDestination: (StudioClip, ClipEdit, Uri) -> Unit,
    onCancelExport: () -> Unit,
    onClearResult: () -> Unit,
) {
    val context = LocalContext.current
    var editingUri by rememberSaveable { mutableStateOf<String?>(null) }
    var editing by remember { mutableStateOf<StudioClip?>(null) }
    var timelineOpen by rememberSaveable { mutableStateOf(false) }
    if (timelineOpen) {
        TimelineEditor(state = editor, exportPhase = state.phase, actions = editorActions, onClose = { timelineOpen = false })
        return
    }
    LaunchedEffect(editingUri) {
        val wanted: String? = editingUri
        when {
            wanted == null -> editing = null
            editing?.uri != wanted -> onDescribe(Uri.parse(wanted)) { editing = it }
        }
    }

    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                onDescribe(uri) {
                    editing = it
                    editingUri = it.uri
                }
            }
        }

    val chooserTitle = stringResource(R.string.studio_share_chooser)
    Column(Modifier.fillMaxSize()) {
        GlassTopBar(title = stringResource(if (editing == null) R.string.nav_studio else R.string.studio_edit))
        val clip = editing
        if (clip == null) {
            Row(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                GlassButton(text = stringResource(R.string.editor_open), onClick = { timelineOpen = true })
            }
            ClipLibrary(
                studio = state,
                onOpen = {
                    editing = it
                    editingUri = it.uri
                },
                onPick = { picker.launch(arrayOf("video/*")) },
                onShare = { context.shareVideo(it, chooserTitle) },
                onRename = { target, name, done -> onRename(target.uri, name, done) },
                onDelete = { target, done -> onDelete(target.uri, done) },
            )
        } else {
            ClipEditor(
                clip = clip,
                studio = state,
                onExport = onExport,
                onExportToDestination = onExportToDestination,
                onCancelExport = onCancelExport,
                onClearResult = onClearResult,
                onClose = {
                    onClearResult()
                    editingUri = null
                },
            )
        }
    }
}

// ---------------------------------------------------------------- clip library

@Composable
private fun ClipLibrary(
    studio: StudioUiState,
    onOpen: (StudioClip) -> Unit,
    onPick: () -> Unit,
    onShare: (Uri) -> Unit,
    onRename: (StudioClip, String, (Boolean) -> Unit) -> Unit,
    onDelete: (StudioClip, (Boolean) -> Unit) -> Unit,
) {
    var renaming by remember { mutableStateOf<StudioClip?>(null) }
    var deleting by remember { mutableStateOf<StudioClip?>(null) }
    var notice by remember { mutableStateOf<String?>(null) }
    val renameFailed = stringResource(R.string.studio_rename_failed)
    val deleteFailed = stringResource(R.string.studio_delete_failed)
    ClipList(
        studio = studio,
        onOpen = onOpen,
        onPick = onPick,
        onShare = onShare,
        onRenameRequest = { renaming = it },
        onDeleteRequest = { deleting = it },
    )

    renaming?.let { clip ->
        RenameClipDialog(
            clip = clip,
            onConfirm = { name ->
                onRename(clip, name) { ok -> notice = if (ok) null else renameFailed }
                renaming = null
            },
            onDismiss = { renaming = null },
        )
    }

    deleting?.let { clip ->
        DeleteClipDialog(
            clip = clip,
            onConfirm = {
                onDelete(clip) { ok -> notice = if (ok) null else deleteFailed }
                deleting = null
            },
            onDismiss = { deleting = null },
        )
    }

    notice?.let { message ->
        StudioNoticeDialog(message = message, onDismiss = { notice = null })
    }
}

@Composable
private fun ClipList(
    studio: StudioUiState,
    onOpen: (StudioClip) -> Unit,
    onPick: () -> Unit,
    onShare: (Uri) -> Unit,
    onRenameRequest: (StudioClip) -> Unit,
    onDeleteRequest: (StudioClip) -> Unit,
) {
    LazyColumn(
        Modifier.fillMaxSize().padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
        contentPadding = PaddingValues(vertical = 12.dp),
    ) {
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GlassButton(text = stringResource(R.string.studio_open_video), tint = GlassPalette.mint, onClick = onPick)
            }
        }
        if (studio.clips.isEmpty() && studio.phase != ExportPhase.Loading) {
            item { ClipLibraryEmpty() }
        }
        items(studio.clips.size) { index ->
            val clip = studio.clips[index]
            ClipRow(
                clip = clip,
                onOpen = { onOpen(clip) },
                onShare = { onShare(Uri.parse(clip.uri)) },
                onRename = { onRenameRequest(clip) },
                onDelete = { onDeleteRequest(clip) },
            )
        }
        if (studio.clips.isNotEmpty()) {
            item { ClipStorageFooter(studio.clips) }
        }
    }
}

@Composable
private fun ClipLibraryEmpty() {
    Column(
        Modifier
            .fillMaxWidth()
            .glassSurface(shape = GlassShapes.tile)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(stringResource(R.string.studio_empty_title), style = MaterialTheme.typography.titleSmall, color = GlassPalette.textPrimary)
        Text(
            stringResource(R.string.studio_empty_body),
            style = MaterialTheme.typography.bodyMedium,
            color = GlassPalette.textSecondary,
        )
    }
}

/** A clip in the library, shown as a glass tile with its artwork thumbnail. */
@Composable
private fun ClipRow(
    clip: StudioClip,
    onOpen: () -> Unit,
    onShare: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit,
) {
    GlassTile(onClick = onOpen, modifier = Modifier.fillMaxWidth(), contentPadding = PaddingValues(10.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(96.dp).height(56.dp).glassSurface(shape = RoundedCornerShape(12.dp))) {
                VideoFrame(clip.uri, atMs = clip.durationMs / 3, modifier = Modifier.fillMaxSize())
            }
            Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                Text(
                    clip.name,
                    style = MaterialTheme.typography.bodyMedium,
                    color = GlassPalette.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    clip.summary(),
                    style = MaterialTheme.typography.labelSmall,
                    color = GlassPalette.textSecondary,
                )
            }
            GlassButton(text = stringResource(R.string.studio_send), onClick = onShare)
            GlassButton(text = stringResource(R.string.action_rename), modifier = Modifier.padding(start = 6.dp), onClick = onRename)
            GlassButton(
                text = stringResource(R.string.action_delete),
                tint = GlassPalette.pink,
                modifier = Modifier.padding(start = 6.dp),
                onClick = onDelete,
            )
        }
    }
}

@Composable
private fun ClipStorageFooter(clips: List<StudioClip>) {
    val bytes = clips.sumOf { it.sizeBytes }
    Text(
        pluralStringResource(R.plurals.clip_count, clips.size, clips.size) +
            ", " +
            stringResource(
                R.string.studio_storage_used,
                stringResource(R.string.studio_size_gb, bytes / (1024f * 1024f * 1024f)),
            ),
        style = MaterialTheme.typography.labelSmall,
        color = GlassPalette.textSecondary,
    )
}

/** A glass card dialog with a title and a row of actions; used where the body needs more than a
 * plain message (here, a text field), so [dev.geode.ui.glass.GlassDialog] does not fit. */
@Composable
private fun StudioGlassDialog(
    onDismissRequest: () -> Unit,
    title: String,
    modifier: Modifier = Modifier,
    body: @Composable () -> Unit = {},
    actions: @Composable () -> Unit,
) {
    Dialog(onDismissRequest = onDismissRequest) {
        Column(
            modifier
                .glassSurface(shape = GlassShapes.tile)
                .padding(24.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleLarge, color = GlassPalette.textPrimary)
            body()
            Row(
                Modifier.padding(top = 20.dp).fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                actions()
            }
        }
    }
}

@Composable
private fun RenameClipDialog(
    clip: StudioClip,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember(clip.uri) { mutableStateOf(clip.name.substringBeforeLast('.')) }
    StudioGlassDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.studio_rename_title),
        body = {
            GlassTextField(
                value = name,
                onValueChange = { name = it },
                placeholder = stringResource(R.string.studio_rename_field),
                modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
            )
        },
        actions = {
            GlassButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
            GlassButton(
                text = stringResource(R.string.action_rename),
                enabled = name.isNotBlank(),
                tint = GlassPalette.mint,
                modifier = Modifier.padding(start = 8.dp),
                onClick = { onConfirm(name) },
            )
        },
    )
}

@Composable
private fun DeleteClipDialog(
    clip: StudioClip,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    StudioGlassDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.studio_delete_title),
        body = {
            Text(
                stringResource(R.string.studio_delete_body, clip.name),
                Modifier.padding(top = 12.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = GlassPalette.textSecondary,
            )
        },
        actions = {
            GlassButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
            GlassButton(
                text = stringResource(R.string.action_delete),
                tint = GlassPalette.pink,
                modifier = Modifier.padding(start = 8.dp),
                onClick = onConfirm,
            )
        },
    )
}

@Composable
private fun StudioNoticeDialog(
    message: String,
    onDismiss: () -> Unit,
) {
    StudioGlassDialog(
        onDismissRequest = onDismiss,
        title = stringResource(R.string.studio_notice_title),
        body = {
            Text(message, Modifier.padding(top = 12.dp), style = MaterialTheme.typography.bodyMedium, color = GlassPalette.textSecondary)
        },
        actions = { GlassButton(text = stringResource(R.string.action_ok), onClick = onDismiss) },
    )
}

// ----------------------------------------------------------------- clip editor

@Composable
private fun ClipEditor(
    clip: StudioClip,
    studio: StudioUiState,
    onExport: (StudioClip, ClipEdit) -> Unit,
    onExportToDestination: (StudioClip, ClipEdit, Uri) -> Unit,
    onCancelExport: () -> Unit,
    onClearResult: () -> Unit,
    onClose: () -> Unit,
) {
    var edit by remember(clip.uri) { mutableStateOf(ClipEdit()) }
    val duration = clip.durationMs.coerceAtLeast(1L)
    val dismiss = rememberPredictiveDismiss(onDismiss = onClose)
    // Mirrors ExportHost's destination picker: below API 29 StudioExporter.publish cannot insert
    // into MediaStore, so the render button forces this picker there instead of failing silently.
    val destinationPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("video/mp4")) { uri ->
            if (uri != null) onExportToDestination(clip, edit, uri)
        }

    LazyColumn(
        Modifier.fillMaxSize().dismissTransform(dismiss).padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
        contentPadding = PaddingValues(vertical = 12.dp),
    ) {
        item {
            ClipEditorHeader(
                clip = clip,
                resettable = !edit.isIdentity(duration),
                onReset = { edit = ClipEdit() },
                onClose = onClose,
            )
        }
        item { ClipEditorPreview(clip = clip, edit = edit) }
        item { ClipCutSection(clip = clip, edit = edit, duration = duration, onEdit = { edit = it }) }
        item { ClipLookSection(edit = edit, onEdit = { edit = it }) }
        item { ClipFrameSection(edit = edit, onEdit = { edit = it }) }
        item { ClipSoundSection(edit = edit, onEdit = { edit = it }) }
        item {
            ClipRenderSection(
                phase = studio.phase,
                canExport = edit.trimmedMs(duration) > 0,
                atDefaults = edit.isIdentity(duration),
                onExport = {
                    if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
                        destinationPicker.launch("geode_studio_${System.currentTimeMillis()}.mp4")
                    } else {
                        onExport(clip, edit)
                    }
                },
                onExportToDestination = {
                    destinationPicker.launch("geode_studio_${System.currentTimeMillis()}.mp4")
                },
                onCancelExport = onCancelExport,
                onClearResult = onClearResult,
            )
        }
    }
}

@Composable
private fun ClipEditorHeader(
    clip: StudioClip,
    resettable: Boolean,
    onReset: () -> Unit,
    onClose: () -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(
                clip.name,
                style = MaterialTheme.typography.bodyMedium,
                color = GlassPalette.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                clip.summary(),
                style = MaterialTheme.typography.labelSmall,
                color = GlassPalette.textSecondary,
            )
        }
        if (resettable) {
            GlassButton(text = stringResource(R.string.studio_reset), onClick = onReset)
        }
        GlassButton(text = stringResource(R.string.action_back), modifier = Modifier.padding(start = 6.dp), onClick = onClose)
    }
}

@Composable
@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
private fun ClipEditorPreview(
    clip: StudioClip,
    edit: ClipEdit,
) {
    Box(Modifier.fillMaxWidth().glassSurface(shape = GlassShapes.tile).padding(4.dp)) {
        ClipPreview(clip, edit)
    }
    Text(
        stringResource(R.string.studio_preview_hint),
        style = MaterialTheme.typography.labelSmall,
        color = GlassPalette.textSecondary,
    )
}

@Composable
private fun ClipCutSection(
    clip: StudioClip,
    edit: ClipEdit,
    duration: Long,
    onEdit: (ClipEdit) -> Unit,
) {
    val outEnd = if (edit.endMs > 0) edit.endMs else duration
    StudioSection(stringResource(R.string.studio_section_cut)) {
        Row(
            Modifier.fillMaxWidth().height(56.dp).glassSurface(shape = RoundedCornerShape(10.dp)).padding(3.dp),
            horizontalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            repeat(FILMSTRIP_FRAMES) { i ->
                VideoFrame(
                    clip.uri,
                    atMs = duration * i / FILMSTRIP_FRAMES,
                    modifier = Modifier.weight(1f).fillMaxSize(),
                    corner = 4.dp,
                )
            }
        }
        ClipTrimSlider(
            value = edit.startMs.toFloat()..outEnd.toFloat(),
            onValueChange = { range ->
                onEdit(
                    edit.copy(
                        startMs = range.start.toLong().coerceIn(0L, duration),
                        endMs = if (range.endInclusive >= duration - 1) 0L else range.endInclusive.toLong(),
                    ),
                )
            },
            valueRange = 0f..duration.toFloat(),
        )
        ClipTrimSummary(edit = edit, duration = duration, outEnd = outEnd)
    }
}

/**
 * The trim range, styled to match [dev.geode.ui.glass.GlassSlider]'s pearl thumbs and iridescent
 * fill: an [RangeSlider] with a glass track and two pearl thumbs, since the shared glass primitives
 * (`ui/glass/`) do not include a dual-thumb slider.
 */
@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun ClipTrimSlider(
    value: ClosedFloatingPointRange<Float>,
    onValueChange: (ClosedFloatingPointRange<Float>) -> Unit,
    valueRange: ClosedFloatingPointRange<Float>,
) {
    val pearlThumb: @Composable (androidx.compose.material3.RangeSliderState) -> Unit = {
        Box(Modifier.size(20.dp).glassSurface(shape = GlassShapes.bubble, tint = GlassPalette.mint, glow = 0.5f))
    }
    RangeSlider(
        value = value,
        onValueChange = onValueChange,
        modifier = Modifier.fillMaxWidth(),
        valueRange = valueRange,
        startThumb = pearlThumb,
        endThumb = pearlThumb,
        track = { state ->
            val span = valueRange.endInclusive - valueRange.start
            val startFraction = if (span > 0f) (state.activeRangeStart - valueRange.start) / span else 0f
            val endFraction = if (span > 0f) (state.activeRangeEnd - valueRange.start) / span else 0f
            Box(Modifier.fillMaxWidth().height(14.dp)) {
                Box(Modifier.matchParentSize().glassSurface(shape = GlassShapes.pill))
                Canvas(Modifier.matchParentSize()) {
                    val y = size.height / 2f
                    val startX = size.width * startFraction.coerceIn(0f, 1f)
                    val endX = size.width * endFraction.coerceIn(0f, 1f)
                    if (endX > startX) {
                        drawLine(
                            brush =
                                Brush.horizontalGradient(
                                    listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach),
                                    startX = startX,
                                    endX = endX,
                                ),
                            start = Offset(startX, y),
                            end = Offset(endX, y),
                            strokeWidth = 6.dp.toPx(),
                            cap = StrokeCap.Round,
                            alpha = 0.85f,
                        )
                    }
                }
            }
        },
    )
}

@Composable
private fun ClipTrimSummary(
    edit: ClipEdit,
    duration: Long,
    outEnd: Long,
) {
    Text(
        stringResource(
            R.string.studio_trim_summary,
            clock(edit.startMs),
            clock(outEnd),
            clock(edit.trimmedMs(duration)),
        ) +
            if (edit.speed != 1f) {
                stringResource(R.string.studio_trim_renders, clock(edit.outputMs(duration)))
            } else {
                ""
            },
        style = MaterialTheme.typography.labelMedium,
        color = GlassPalette.mint,
    )
}

@Composable
private fun ClipLookSection(
    edit: ClipEdit,
    onEdit: (ClipEdit) -> Unit,
) {
    val context = LocalContext.current
    val lutPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                onEdit(edit.copy(lutUri = uri.toString()))
            }
        }
    StudioSection(stringResource(R.string.studio_section_look)) {
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ClipLook.entries.forEach { look ->
                StudioChip(look.label, selected = edit.look == look) {
                    onEdit(look.applyTo(edit).copy(look = look))
                }
            }
        }
        StudioSlider(stringResource(R.string.studio_brightness), edit.brightness, -0.5f..0.5f) {
            onEdit(edit.copy(brightness = it))
        }
        StudioSlider(stringResource(R.string.studio_contrast), edit.contrast, -0.6f..0.6f) {
            onEdit(edit.copy(contrast = it))
        }
        StudioSlider(
            stringResource(R.string.studio_saturation),
            edit.saturation,
            -100f..100f,
            unit = "%",
        ) { onEdit(edit.copy(saturation = it)) }
        StudioSlider(
            stringResource(R.string.studio_hue_shift),
            edit.hueDegrees,
            -180f..180f,
            unit = "°",
        ) { onEdit(edit.copy(hueDegrees = it)) }
        Text(
            stringResource(R.string.studio_look_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = GlassPalette.textSecondary,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            GlassButton(text = stringResource(R.string.studio_lut_pick), onClick = { lutPicker.launch(arrayOf("*/*")) })
            if (edit.lutUri != null) {
                GlassButton(text = stringResource(R.string.studio_lut_clear), onClick = { onEdit(edit.copy(lutUri = null)) })
            }
        }
        Text(
            edit.lutUri?.let { stringResource(R.string.studio_lut_loaded, it.substringAfterLast('/').substringAfterLast(':')) }
                ?: stringResource(R.string.studio_lut_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = GlassPalette.textSecondary,
        )
        StudioSlider(stringResource(R.string.studio_gamma_red), edit.gammaRed, GAMMA_RANGE, decimals = 2) {
            onEdit(edit.copy(gammaRed = it))
        }
        StudioSlider(stringResource(R.string.studio_gamma_green), edit.gammaGreen, GAMMA_RANGE, decimals = 2) {
            onEdit(edit.copy(gammaGreen = it))
        }
        StudioSlider(stringResource(R.string.studio_gamma_blue), edit.gammaBlue, GAMMA_RANGE, decimals = 2) {
            onEdit(edit.copy(gammaBlue = it))
        }
    }
}

private val GAMMA_RANGE = 0.5f..2f

@Composable
private fun ClipFrameSection(
    edit: ClipEdit,
    onEdit: (ClipEdit) -> Unit,
) {
    StudioSection(stringResource(R.string.studio_section_frame)) {
        StudioSlider(stringResource(R.string.studio_speed), edit.speed, 0.25f..4f, unit = "×", decimals = 2) {
            onEdit(edit.copy(speed = it))
        }
        StudioSlider(stringResource(R.string.studio_rotate), edit.rotationDegrees, -180f..180f, unit = "°") {
            onEdit(edit.copy(rotationDegrees = it))
        }
        Text(stringResource(R.string.studio_reframe), style = MaterialTheme.typography.labelMedium, color = GlassPalette.textSecondary)
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            StudioChip(stringResource(R.string.studio_as_shot), selected = edit.ratio == null) { onEdit(edit.copy(ratio = null)) }
            ExportRatio.entries.forEach { r ->
                StudioChip(r.label, selected = edit.ratio == r) { onEdit(edit.copy(ratio = r)) }
            }
        }
        if (edit.ratio != null) {
            ClipQualityPicker(edit = edit, onEdit = onEdit)
        }
    }
}

@Composable
private fun ClipQualityPicker(
    edit: ClipEdit,
    onEdit: (ClipEdit) -> Unit,
) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        ExportQuality.entries.forEach { q ->
            StudioChip("${q.shortSide}p", selected = edit.quality == q) { onEdit(edit.copy(quality = q)) }
        }
    }
    Text(
        stringResource(R.string.studio_reframe_explainer),
        style = MaterialTheme.typography.bodySmall,
        color = GlassPalette.textSecondary,
    )
}

@Composable
private fun ClipSoundSection(
    edit: ClipEdit,
    onEdit: (ClipEdit) -> Unit,
) {
    StudioSection(stringResource(R.string.studio_section_sound)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                stringResource(R.string.studio_mute),
                Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
                color = GlassPalette.textPrimary,
            )
            GlassToggle(checked = edit.mute, onCheckedChange = { onEdit(edit.copy(mute = it)) })
        }
        GlassTextField(
            value = edit.caption,
            onValueChange = { onEdit(edit.copy(caption = it)) },
            placeholder = stringResource(R.string.studio_caption),
            modifier = Modifier.fillMaxWidth(),
        )
        Text(
            stringResource(R.string.studio_caption_explainer),
            style = MaterialTheme.typography.bodySmall,
            color = GlassPalette.textSecondary,
        )
    }
}

@Composable
private fun ClipRenderSection(
    phase: ExportPhase,
    canExport: Boolean,
    atDefaults: Boolean,
    onExport: () -> Unit,
    onExportToDestination: () -> Unit,
    onCancelExport: () -> Unit,
    onClearResult: () -> Unit,
) {
    StudioSection(stringResource(R.string.studio_section_render)) {
        when (phase) {
            is ExportPhase.Running -> ClipEditorRunning(phase.progress, onCancelExport)
            is ExportPhase.Done -> ClipEditorDone(phase.resultUri, onClearResult)
            ExportPhase.Idle, ExportPhase.Loading, is ExportPhase.Failed ->
                ClipEditorIdle(
                    message = phase.errorOrNull,
                    canExport = canExport,
                    atDefaults = atDefaults,
                    onExport = onExport,
                    onExportToDestination = onExportToDestination,
                )
        }
    }
}

@Composable
private fun ClipEditorRunning(
    progress: Float,
    onCancel: () -> Unit,
) {
    GlassLinearProgress(progress = progress, modifier = Modifier.fillMaxWidth())
    Text(
        stringResource(R.string.studio_rendering, (progress * 100).roundToInt()),
        style = MaterialTheme.typography.labelMedium,
        color = GlassPalette.textSecondary,
    )
    GlassButton(text = stringResource(R.string.action_cancel), onClick = onCancel)
}

@Composable
private fun ClipEditorDone(
    resultUri: Uri,
    onClearResult: () -> Unit,
) {
    val context = LocalContext.current
    val chooserTitle = stringResource(R.string.studio_share_chooser)
    // No clip title reaches this composable, so the rendered file's own name stands in for
    // EXTRA_TITLE/SUBJECT, same as the export dialog's share button.
    val resultName = resultUri.lastPathSegment?.substringAfterLast('/')
    Text(stringResource(R.string.studio_saved), style = MaterialTheme.typography.bodyMedium, color = GlassPalette.textPrimary)
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        GlassButton(
            text = stringResource(R.string.studio_send_ellipsis),
            tint = GlassPalette.mint,
            onClick = { context.shareVideo(resultUri, chooserTitle, title = resultName, subject = resultName) },
        )
        GlassButton(text = stringResource(R.string.studio_play), onClick = { context.viewVideo(resultUri) })
        GlassButton(text = stringResource(R.string.studio_edit_again), onClick = onClearResult)
    }
    Text(
        stringResource(R.string.studio_send_explainer),
        style = MaterialTheme.typography.bodySmall,
        color = GlassPalette.textSecondary,
    )
}

@Composable
private fun ClipEditorIdle(
    message: String?,
    canExport: Boolean,
    atDefaults: Boolean,
    onExport: () -> Unit,
    onExportToDestination: () -> Unit,
) {
    message?.let {
        Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
    }
    if (atDefaults) {
        Text(
            stringResource(R.string.studio_nothing_changed),
            style = MaterialTheme.typography.bodySmall,
            color = GlassPalette.textSecondary,
        )
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        GlassButton(text = stringResource(R.string.studio_render), enabled = canExport, tint = GlassPalette.mint, onClick = onExport)
        GlassButton(text = stringResource(R.string.export_render_to_folder), enabled = canExport, onClick = onExportToDestination)
    }
    Text(
        stringResource(R.string.studio_renders_new_file),
        style = MaterialTheme.typography.bodySmall,
        color = GlassPalette.textSecondary,
    )
}

private const val FILMSTRIP_FRAMES = 6

/** A section of the clip editor: a glass tile with a title and its controls. */
@Composable
private fun StudioSection(
    title: String,
    content: @Composable () -> Unit,
) {
    Column(
        Modifier
            .fillMaxWidth()
            .glassSurface(shape = GlassShapes.tile)
            .floatOnWater(strength = 0.3f)
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text(title, style = MaterialTheme.typography.titleSmall, color = GlassPalette.textPrimary)
        content()
    }
}

@Composable
private fun StudioSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    unit: String = "",
    decimals: Int = 0,
    onChange: (Float) -> Unit,
) {
    Column {
        Text(
            stringResource(R.string.studio_slider_value, label, "%.${decimals}f".format(value), unit),
            style = MaterialTheme.typography.labelMedium,
            color = GlassPalette.textSecondary,
        )
        GlassSlider(value = value, onValueChange = onChange, valueRange = range)
    }
}

/** A pastel-tinted glass pill; selecting it swells the tint, matching a segmented choice chip. */
@Composable
private fun StudioChip(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    GlassButton(
        text = label,
        selected = selected,
        tint = if (selected) GlassPalette.lavender else null,
        onClick = onClick,
    )
}

private fun clock(ms: Long): String = "%d:%02d".format(ms / 60_000, (ms / 1000) % 60)

// internal (not private) so the export dialog (SettingsDialog.kt, same package) can share this
// one implementation instead of building its own SEND intent.
internal fun android.content.Context.shareVideo(
    uri: Uri,
    chooserTitle: String,
    title: String? = null,
    subject: String? = null,
) {
    val send =
        Intent(Intent.ACTION_SEND)
            .setType("video/mp4")
            .putExtra(Intent.EXTRA_STREAM, uri)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            .apply {
                // ClipData mirrors EXTRA_STREAM so share targets that read ClipData rather than
                // the intent extra (most do, for a preview/thumbnail) still see the video.
                clipData = ClipData.newUri(contentResolver, title ?: chooserTitle, uri)
                if (title != null) putExtra(Intent.EXTRA_TITLE, title)
                if (subject != null) putExtra(Intent.EXTRA_SUBJECT, subject)
            }
    runCatching { startActivity(Intent.createChooser(send, chooserTitle)) }
}

private fun android.content.Context.viewVideo(uri: Uri) {
    val view =
        Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, "video/mp4")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    runCatching { startActivity(view) }
}
