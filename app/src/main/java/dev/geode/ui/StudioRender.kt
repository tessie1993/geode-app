package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.Preset
import dev.geode.data.exportCodecLabel
import dev.geode.data.exportQualityLabel
import dev.geode.data.filledFrom
import dev.geode.export.BatchTrack
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportPreset
import dev.geode.export.ExportPresets
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRange
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget
import dev.geode.export.RenderPlaylist
import dev.geode.export.RenderScope
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.pro.ProGate

/**
 * The Studio destination: Edit (the Pro clip editor) and Render (visualizer export, loop render
 * and the export options). Studio is the one owner of everything that makes a video; Settings
 * only links here. Only Edit sits behind [ProGate]: Render stays open to free and trial users,
 * whose limits (length, resolution, frame rate, watermark) are enforced where each render starts,
 * in `PlayerViewModel`, by the same `FeatureGate` as before.
 */
@Composable
fun StudioHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    section: StudioSection,
    onSection: (StudioSection) -> Unit,
) {
    Column(Modifier.fillMaxSize()) {
        Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
            CrystalOverline(stringResource(R.string.app_name))
            GlowTitle(stringResource(R.string.nav_studio))
        }
        CrystalTabs(
            titles =
                listOf(
                    stringResource(R.string.studio_edit),
                    stringResource(R.string.studio_render),
                    stringResource(R.string.studio_takes),
                ),
            selected = section.ordinal,
            onSelect = { onSection(StudioSection.entries[it]) },
        )
        when (section) {
            StudioSection.EDIT -> ProGate(ProFeature.STUDIO) { StudioRoute() }
            StudioSection.RENDER -> StudioRenderTab(viewModel, visualizerView)
            StudioSection.TAKES -> TakesTab(geodeViewModel())
        }
    }
}

/**
 * Studio › Render: one page. The options (remembered between renders), then what a render covers
 * and where it goes, and at the bottom [RenderBar] with the one Render button and, afterwards, the
 * finished file. It used to be two buttons opening a dialog, above a block of "defaults" that read
 * like the settings themselves, so the render the user wanted was never on screen.
 */
@Composable
internal fun StudioRenderTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val paywall: PaywallViewModel = geodeViewModel()
    val studioViewModel: StudioViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val player by viewModel.uiState.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val export by studioViewModel.exportState.collectAsStateWithLifecycle()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val store = remember { ExportPrefsStore(GeodePrefsFiles(context).general) }
    var options by remember { mutableStateOf(store.load()) }
    val update: (ExportDefaults) -> Unit = {
        options = it
        store.save(it)
    }
    var length by rememberSaveable(stateSaver = RenderLengthSaver) { mutableStateOf(RenderLength()) }
    var askWhere by rememberSaveable { mutableStateOf(false) }
    var loopOpen by rememberSaveable { mutableStateOf(false) }
    val startExport = rememberVisualizerExport(viewModel, visualizerView)

    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val queue by viewModel.queue.collectAsStateWithLifecycle()
    val deviceTracks by libraryViewModel.deviceTracks.collectAsStateWithLifecycle()
    val library by libraryViewModel.library.collectAsStateWithLifecycle()
    var selection by rememberSaveable(stateSaver = RenderSelectionSaver) { mutableStateOf(RenderSelection()) }
    var picking by rememberSaveable { mutableStateOf<RenderPick?>(null) }
    val libraryTracks = remember(deviceTracks) { deviceTracks.map { BatchTrack(it.uri, it.title) } }
    val titles = remember(libraryTracks) { libraryTracks.associate { it.uri to it.title } }
    // Smart playlists are resolved only while their list can be seen or one of them is chosen.
    val playlists =
        remember(library, deviceTracks, picking, selection.scope) {
            if (picking != RenderPick.PLAYLIST && selection.scope != RenderScope.PLAYLIST) {
                emptyList()
            } else {
                library.playlists.map { p ->
                    RenderPlaylist(p.name, p.trackUris.map { BatchTrack(it, titles[it] ?: it.substringAfterLast('/')) })
                } +
                    library.smartPlaylists.map { p ->
                        RenderPlaylist(p.name, libraryViewModel.resolveSmartPlaylist(p).map { BatchTrack(it.uri, it.title) })
                    }
            }
        }
    val batchTracks =
        RenderScope.tracksFor(
            selection.scope,
            current = null,
            queue = queue.tracks.map { BatchTrack(it.uri, it.title.ifBlank { it.uri.substringAfterLast('/') }) },
            playlist = playlists.firstOrNull { it.name == selection.playlistName }?.tracks.orEmpty(),
            chosen = selection.chosenUris.map { BatchTrack(it, titles[it] ?: it.substringAfterLast('/')) },
        )
    val lookPresets =
        remember(viz.presets) {
            (viz.presets + BuiltInPresets.ALL)
                .filter { it.customShader == null && it.milkPreset == null }
                .distinctBy { it.name }
        }
    Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f)) {
            SettingsTabColumn {
                if (!gate.isPro) {
                    item { OptionHint(stringResource(R.string.studio_render_free_limits)) }
                }
                item { RenderVideoGroup(options, update) }
                item { RenderSoundGroup(options, update) }
                if (selection.scope.isBatch) {
                    item {
                        RenderLookGroup(lookPresets, selection.lookName) { selection = selection.copy(lookName = it) }
                    }
                    item { OptionHint(stringResource(R.string.render_batch_hint)) }
                } else {
                    item {
                        RenderLengthGroup(
                            length = length,
                            onLength = { length = it },
                            trackDurationMs = player.durationMs,
                            takes = takes.takes.map { it.name },
                            selectedTake = takes.exportTake,
                            onSelectTake = studioViewModel::setExportTake,
                        )
                    }
                    item {
                        SettingsGroup(stringResource(R.string.render_group_save)) {
                            RenderChipRow {
                                RenderChip(stringResource(R.string.render_save_library), !askWhere) { askWhere = false }
                                RenderChip(stringResource(R.string.render_save_ask), askWhere) { askWhere = true }
                            }
                        }
                    }
                }
                item {
                    SettingsGroup(stringResource(R.string.export_loop_title)) {
                        OptionHint(stringResource(R.string.export_loop_source_hint))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                            CrystalButton(filled = false, onClick = { loopOpen = true }) {
                                Text(stringResource(R.string.export_loop_menu_loop))
                            }
                            if (!gate.allows(ProFeature.FULL_LENGTH_EXPORT)) ProBadge()
                        }
                    }
                }
            }
        }
        RenderBar(
            export = export,
            scope = { modifier ->
                RenderScopeChooser(
                    scope = selection.scope,
                    summary =
                        if (selection.scope.isBatch) {
                            listOfNotNull(
                                selection.playlistName.takeIf { selection.scope == RenderScope.PLAYLIST },
                                pluralStringResource(R.plurals.render_tracks_count, batchTracks.size, batchTracks.size),
                            ).joinToString(" · ")
                        } else {
                            player.title?.let { title -> listOfNotNull(title, player.artist).joinToString(" · ") }
                                ?: stringResource(R.string.render_nothing_playing)
                        },
                    batchLocked = !gate.allows(ProFeature.FULL_LENGTH_EXPORT),
                    onScope = { selection = selection.copy(scope = it) },
                    onPickPlaylist = { picking = RenderPick.PLAYLIST },
                    onPickTracks = { picking = RenderPick.TRACKS },
                    modifier = modifier,
                )
            },
            renderLabel = stringResource(R.string.render_button),
            canRender = if (selection.scope.isBatch) batchTracks.isNotEmpty() else player.hasMedia,
            onRender = render@{
                if (selection.scope.isBatch) {
                    val look = lookPresets.firstOrNull { it.name == selection.lookName }
                    val renderer = visualizerView.visualizerRenderer
                    val sceneId = look?.sceneId?.takeIf { it in renderer.availableSceneIds() } ?: viz.sceneId
                    viewModel.startBatch(
                        tracks = batchTracks,
                        aspect = ExportAspect.of(options.quality, options.ratio),
                        fps = options.fps,
                        sceneFactory = renderer.exportSceneFactory(sceneId),
                        sceneParams = look?.params,
                        codec = options.codec,
                    )
                    return@render
                }
                val range = length.rangeFor(player.durationMs)
                startExport(
                    PendingExport(
                        aspect = ExportAspect.of(options.quality, options.ratio),
                        fps = options.fps,
                        sceneId = viz.sceneId,
                        rangeStartMs = range?.startMs ?: 0L,
                        rangeDurationMs = range?.durationMs ?: 0L,
                        codec = options.codec,
                    ),
                    askWhere,
                )
            },
            onCancel = studioViewModel::cancelExport,
            onClearResult = studioViewModel::resetExportState,
        )
    }
    if (loopOpen) ExportHost(viewModel, visualizerView) { loopOpen = false }
    when (picking) {
        RenderPick.PLAYLIST ->
            PlaylistPickerDialog(
                playlists = playlists,
                onPick = { name ->
                    selection = selection.copy(scope = RenderScope.PLAYLIST, playlistName = name)
                    picking = null
                },
                onDismiss = { picking = null },
            )
        RenderPick.TRACKS ->
            TrackChecklistDialog(
                tracks = libraryTracks,
                chosen = selection.chosenUris,
                onDone = { uris ->
                    selection = selection.copy(scope = RenderScope.CHOSEN_TRACKS, chosenUris = uris)
                    picking = null
                },
                onDismiss = { picking = null },
            )
        null -> Unit
    }
}

/** Which picker the Render bar's scope menu has open. */
private enum class RenderPick { PLAYLIST, TRACKS }

/**
 * What the Render button covers and with which look, kept for the life of the screen so a later
 * render reuses the same chosen tracks or playlist ("the choice is kept").
 */
private data class RenderSelection(
    val scope: RenderScope = RenderScope.CURRENT_TRACK,
    val playlistName: String? = null,
    val chosenUris: List<String> = emptyList(),
    /** A preset's name, or null for the current look. */
    val lookName: String? = null,
)

private val RenderSelectionSaver =
    listSaver<RenderSelection, Any>(
        save = { listOf(it.scope.name, it.playlistName.orEmpty(), it.lookName.orEmpty(), ArrayList(it.chosenUris)) },
        restore = { saved ->
            RenderSelection(
                scope = RenderScope.entries.firstOrNull { it.name == saved[0] } ?: RenderScope.CURRENT_TRACK,
                playlistName = (saved[1] as String).ifEmpty { null },
                lookName = (saved[2] as String).ifEmpty { null },
                chosenUris = (saved[3] as List<*>).filterIsInstance<String>(),
            )
        },
    )

/**
 * The look a batch renders with: the current one (null) or one preset for every track. Presets
 * that draw through a custom shader or a Milkdrop file are left out: the offscreen export renders
 * a scene id and its parameters, and those two need the live view's own shader state.
 */
@Composable
private fun RenderLookGroup(
    presets: List<Preset>,
    lookName: String?,
    onLook: (String?) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    SettingsGroup(stringResource(R.string.render_group_look)) {
        Box {
            CrystalButton(filled = false, compact = true, onClick = { open = true }) {
                Text(lookName ?: stringResource(R.string.render_look_current))
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
            }
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.render_look_current)) },
                    onClick = {
                        open = false
                        onLook(null)
                    },
                )
                presets.forEach { preset ->
                    DropdownMenuItem(
                        text = { Text(preset.name) },
                        onClick = {
                            open = false
                            onLook(preset.name)
                        },
                    )
                }
            }
        }
        OptionHint(stringResource(R.string.render_look_hint))
    }
}

/** Whole track, or a segment picked as fractions of the track (so it survives a track change). */
internal data class RenderLength(
    val segment: Boolean = false,
    val start: Float = 0f,
    val end: Float = 1f,
) {
    fun rangeFor(trackDurationMs: Long): ExportRange? =
        if (!segment || trackDurationMs <= 0) {
            null
        } else {
            ExportRange.of(
                startMs = (start * trackDurationMs).toLong(),
                endMs = (end * trackDurationMs).toLong(),
                trackDurationMs = trackDurationMs,
            )
        }
}

private val RenderLengthSaver =
    listSaver<RenderLength, Any>(
        save = { listOf(it.segment, it.start, it.end) },
        restore = { RenderLength(it[0] as Boolean, it[1] as Float, it[2] as Float) },
    )

@Composable
private fun RenderVideoGroup(
    options: ExportDefaults,
    update: (ExportDefaults) -> Unit,
) {
    SettingsGroup(stringResource(R.string.render_group_video)) {
        OptionLabel(stringResource(R.string.export_quality))
        RenderChipRow {
            ExportQuality.entries.forEach { q ->
                RenderChip(exportQualityLabel(q), options.quality == q) { update(options.copy(quality = q)) }
            }
        }
        if (options.quality == ExportQuality.UHD4K) OptionHint(stringResource(R.string.export_4k_fallback))
        OptionLabel(stringResource(R.string.export_frame_rate))
        RenderChipRow {
            EXPORT_FPS_OPTIONS.zip(fpsLabels()).forEach { (fps, label) ->
                RenderChip(label, options.fps == fps) { update(options.copy(fps = fps)) }
            }
        }
        OptionLabel(stringResource(R.string.export_aspect_ratio))
        RenderChipRow {
            ExportRatio.entries.forEach { r ->
                RenderChip(r.label, options.ratio == r) { update(options.copy(ratio = r)) }
            }
        }
        OptionLabel(stringResource(R.string.export_codec))
        RenderChipRow {
            ExportCodec.entries.forEach { c ->
                RenderChip(exportCodecLabel(c), options.codec == c) { update(options.copy(codec = c)) }
            }
        }
        val hevcEncoder = remember { ExportCodec.hasEncoder(ExportCodec.HEVC.mimeType) }
        OptionHint(stringResource(if (hevcEncoder) R.string.export_codec_explainer else R.string.export_codec_no_hevc))
        PlatformFillMenu { update(options.filledFrom(it)) }
    }
}

@Composable
private fun RenderSoundGroup(
    options: ExportDefaults,
    update: (ExportDefaults) -> Unit,
) {
    SettingsGroup(stringResource(R.string.render_group_sound)) {
        OptionLabel(stringResource(R.string.export_loudness_target))
        RenderChipRow {
            LoudnessTarget.ALL.forEach { target ->
                RenderChip(stringResource(target.labelRes), options.loudnessTargetId == target.id) {
                    update(options.copy(loudnessTargetId = target.id))
                }
            }
        }
        OptionHint(stringResource(R.string.export_loudness_hint))
    }
}

@Composable
private fun RenderLengthGroup(
    length: RenderLength,
    onLength: (RenderLength) -> Unit,
    trackDurationMs: Long,
    takes: List<String>,
    selectedTake: String?,
    onSelectTake: (String?) -> Unit,
) {
    SettingsGroup(stringResource(R.string.export_length)) {
        if (trackDurationMs > 0) {
            RenderChipRow {
                RenderChip(stringResource(R.string.export_whole_track), !length.segment) { onLength(length.copy(segment = false)) }
                RenderChip(stringResource(R.string.export_segment), length.segment) { onLength(length.copy(segment = true)) }
            }
            if (length.segment) {
                RangeSlider(
                    value = length.start..length.end,
                    onValueChange = { r -> onLength(length.copy(start = r.start, end = r.endInclusive)) },
                )
                val range = length.rangeFor(trackDurationMs)
                OptionHint(
                    if (range == null) {
                        stringResource(R.string.export_segment_hint, (ExportRange.MIN_DURATION_MS / 1000).toInt())
                    } else {
                        stringResource(
                            R.string.export_segment_summary,
                            formatClock(range.startMs),
                            formatClock(range.endMs),
                            formatClock(range.durationMs),
                        )
                    },
                )
            }
        }
        if (takes.isNotEmpty()) {
            OptionLabel(stringResource(R.string.export_group_performance))
            RenderChipRow {
                RenderChip(stringResource(R.string.export_live_settings), selectedTake == null) { onSelectTake(null) }
                takes.forEach { name -> RenderChip(name, selectedTake == name) { onSelectTake(name) } }
            }
            if (selectedTake != null) OptionHint(stringResource(R.string.export_take_explainer))
        }
    }
}

@Composable
private fun OptionLabel(text: String) {
    Text(text, style = MaterialTheme.typography.labelMedium)
}

@Composable
private fun OptionHint(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/**
 * Every option row wraps instead of scrolling sideways. A horizontally scrolled row started at its
 * left end, so on a phone the selected chip (YouTube, 60 fps, "Leave as-is") sat off-screen.
 */
@Composable
private fun RenderChipRow(content: @Composable () -> Unit) {
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) { content() }
}

@Composable
private fun RenderChip(
    label: String,
    selected: Boolean,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        enabled = enabled,
        label = { Text(label, style = MaterialTheme.typography.labelSmall) },
    )
}

// The frame-rate chips' labels ("24 fps", "25 fps", …).
@Composable
internal fun fpsLabels(): List<String> =
    EXPORT_FPS_OPTIONS.map { fps ->
        when (fps) {
            24 -> stringResource(R.string.export_fps_24)
            25 -> stringResource(R.string.export_fps_25)
            30 -> stringResource(R.string.export_fps_30)
            50 -> stringResource(R.string.export_fps_50)
            else -> stringResource(R.string.export_fps_60)
        }
    }

/**
 * "Fill from platform ▾": sets the options above to a platform's usual combination, once. It sits
 * below the options on purpose: they are what the user picks, and a platform is only a shortcut
 * many people never use. Nothing stays selected afterwards, so a later change never snaps back.
 */
@Composable
internal fun PlatformFillMenu(onFill: (ExportPreset) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Column {
        Box {
            CrystalButton(filled = false, compact = true, onClick = { open = true }) {
                Text(stringResource(R.string.export_fill_from_platform))
                Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
            }
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                ExportPresets.ALL.forEach { preset ->
                    DropdownMenuItem(
                        text = {
                            Text(
                                stringResource(
                                    R.string.export_platform_entry,
                                    stringResource(preset.nameRes),
                                    stringResource(
                                        R.string.export_spec,
                                        preset.ratio.label,
                                        exportQualityLabel(preset.quality),
                                        preset.fps,
                                    ),
                                ),
                            )
                        },
                        onClick = {
                            open = false
                            onFill(preset)
                        },
                    )
                }
            }
        }
        OptionHint(stringResource(R.string.export_fill_from_platform_hint))
    }
}
