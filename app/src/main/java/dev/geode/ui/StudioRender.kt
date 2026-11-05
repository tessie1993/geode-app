package dev.geode.ui

import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.Preset
import dev.geode.data.StudioPrefsStore
import dev.geode.data.exportCodecLabel
import dev.geode.data.exportQualityLabel
import dev.geode.export.BatchTrack
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRange
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget
import dev.geode.export.RenderPlaylist
import dev.geode.export.RenderScope
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.floatingGlass

/**
 * The Studio destination: two tabs, in the order a first visit meets them.
 *
 * - Render makes a video of what is playing: one scrolling stack (Source, Look, Format, Save) over
 *   the sticky [RenderBar]. It stays open to free and trial users, whose limits are enforced where
 *   each render starts, in `PlayerViewModel`, by the same `FeatureGate` as before.
 * - My videos ([MyVideosTab]) lists everything Studio made: finished renders and recorded takes.
 *
 * There is no Edit tab: the owner dropped video editing from the launch (#227). A take still
 * renders to a video in the background, through the project model, without an editor.
 *
 * The tab the user leaves Studio on is kept across restarts ([StudioPrefsStore]).
 */
@Composable
fun StudioHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    section: StudioSection,
    onSection: (StudioSection) -> Unit,
) {
    val context = LocalContext.current
    val prefs = remember { StudioPrefsStore(GeodePrefsFiles(context).general) }
    val paywall: PaywallViewModel = geodeViewModel()
    val studioViewModel: StudioViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val select: (StudioSection) -> Unit = { tab ->
        prefs.saveLastTab(tab.name)
        onSection(tab)
    }
    // The tabs taken through Studio, so back retraces them (the NavHistory pattern Visuals uses)
    // instead of leaving Studio from wherever the user is. A render keeps going either way: it runs
    // under ExportService.
    val route = rememberSaveable(saver = studioRouteSaver) { mutableStateListOf(section) }
    val history = remember(route) { NavHistory(route) }
    LaunchedEffect(section) { history.open(section) }

    BackHandler(enabled = history.canPop) {
        history.pop()
        select(history.top)
    }
    Column(Modifier.fillMaxSize()) {
        GlowTitle(
            stringResource(R.string.nav_studio),
            Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp),
            style = MaterialTheme.typography.titleLarge,
        )
        CrystalTabs(
            titles =
                listOf(
                    stringResource(R.string.studio_render),
                    stringResource(R.string.studio_my_videos),
                ),
            selected = section.ordinal,
            onSelect = { select(StudioSection.entries[it]) },
        )
        when (section) {
            StudioSection.RENDER -> StudioRenderTab(viewModel, visualizerView, gate)
            StudioSection.MY_VIDEOS ->
                MyVideosTab(
                    viewModel = studioViewModel,
                    onRenderTake = { name ->
                        studioViewModel.setExportTake(name)
                        select(StudioSection.RENDER)
                    },
                )
        }
    }
}

/** Studio's route saved by tab name, so a rotation or process death keeps back's history. */
private val studioRouteSaver =
    listSaver<SnapshotStateList<StudioSection>, String>(
        save = { route -> route.map { it.name } },
        // A route saved before Edit was removed may name it; those entries are dropped.
        restore = { names ->
            names
                .mapNotNull(StudioSection::fromName)
                .distinct()
                .ifEmpty { listOf(StudioSection.RENDER) }
                .toMutableStateList()
        },
    )

/**
 * Studio › Render: one page that reads top to bottom as the questions a render asks.
 *
 * - Source: what to render (the song playing, the queue, a playlist, chosen tracks; the last three
 *   are a Pro batch) and, for the song playing, which take replays on it.
 * - Look: the look on stage, or a preset (for one song it is put on stage, so the video shows what
 *   the user sees; for a batch it is used for every track).
 * - Format: a destination (TikTok, Shorts, …) fills in its own defaults from Settings › Export, then
 *   quality, frame rate, ratio, codec, loudness and length can be changed; Pro choices carry ♛.
 * - Save: the Videos library, or a folder asked each time.
 *
 * The Render button sits in [RenderBar], which always repeats the quality, frame rate and save
 * place. There are no hint paragraphs: each control says what it is. Pressing Render with a Pro
 * choice by a free user offers the paywall or a free version of the
 * same video (the free limits, never a dead end); a single render then asks once whether to add it
 * to My videos, unless the user told it to stop asking.
 */
@Composable
internal fun StudioRenderTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    gate: FeatureGate,
) {
    val paywall: PaywallViewModel = geodeViewModel()
    val studioViewModel: StudioViewModel = geodeViewModel()
    val player by viewModel.uiState.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val export by studioViewModel.exportState.collectAsStateWithLifecycle()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val store = remember { ExportPrefsStore(GeodePrefsFiles(context).general) }
    val studioPrefs = remember { StudioPrefsStore(GeodePrefsFiles(context).general) }
    var destinationName by rememberSaveable { mutableStateOf<String?>(null) }
    val destination = destinationName?.let { ExportDestination.byName(it) }
    var options by remember { mutableStateOf(store.load()) }
    val update: (ExportDefaults) -> Unit = {
        options = it
        if (destination == null) store.save(it) else store.saveFor(destination, it)
    }
    var length by rememberSaveable(stateSaver = RenderLengthSaver) { mutableStateOf(RenderLength()) }
    var askWhere by rememberSaveable { mutableStateOf(false) }
    var loopOpen by rememberSaveable { mutableStateOf(false) }
    var proBlock by remember { mutableStateOf<ProFeature?>(null) }
    var keepAsk by remember { mutableStateOf<ExportDefaults?>(null) }
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
    // A native call that copies every candidate URI in, so it runs only when its inputs change.
    val batchTracks =
        remember(selection, queue.tracks, playlists, titles) {
            RenderScope.tracksFor(
                selection.scope,
                current = null,
                queue = queue.tracks.map { BatchTrack(it.uri, it.title.ifBlank { it.uri.substringAfterLast('/') }) },
                playlist = playlists.firstOrNull { it.name == selection.playlistName }?.tracks.orEmpty(),
                chosen = selection.chosenUris.map { BatchTrack(it, titles[it] ?: it.substringAfterLast('/')) },
            )
        }
    val lookPresets =
        remember(viz.presets) {
            (viz.presets + BuiltInPresets.ALL)
                .filter { it.customShader == null && it.milkPreset == null }
                .distinctBy { it.name }
        }

    // The single render itself, after the Pro question and the keep-as-project question.
    val renderOne: (ExportDefaults, Boolean) -> Unit = { chosen, keep ->
        val track = studioViewModel.currentTrack(player.title)
        if (keep && track != null) {
            val milkPath = visualizerView.visualizerRenderer.exportSceneFactory(viz.sceneId).milkPresetPath
            studioViewModel.keepNextRenderAsProject(track, viz.sceneId, milkPath)
        }
        val range = length.rangeFor(player.durationMs)
        startExport(
            PendingExport(
                aspect = ExportAspect.of(chosen.quality, chosen.ratio),
                fps = chosen.fps,
                sceneId = viz.sceneId,
                rangeStartMs = range?.startMs ?: 0L,
                rangeDurationMs = range?.durationMs ?: 0L,
                codec = chosen.codec,
            ),
            askWhere,
        )
    }
    val askKeepThenRender: (ExportDefaults) -> Unit = { chosen ->
        when (val remembered = studioPrefs.keepAsProject()) {
            null -> keepAsk = chosen
            else -> renderOne(chosen, remembered)
        }
    }

    Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f)) {
            SettingsTabColumn {
                item {
                    RenderGroup(stringResource(R.string.render_group_source)) {
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
                            batchSupported = RenderScope.batchSupported(Build.VERSION.SDK_INT),
                            onScope = { selection = selection.copy(scope = it) },
                            onPickPlaylist = { picking = RenderPick.PLAYLIST },
                            onPickTracks = { picking = RenderPick.TRACKS },
                        )
                        if (!selection.scope.isBatch && takes.takes.isNotEmpty()) {
                            RenderTakeChoice(
                                takes = takes.takes.map { it.name },
                                selected = takes.exportTake,
                                onSelect = studioViewModel::setExportTake,
                            )
                        }
                    }
                }
                item {
                    RenderLookGroup(
                        presets = lookPresets,
                        lookName = selection.lookName,
                    ) { name ->
                        selection = selection.copy(lookName = name)
                        // One song renders what is on stage, so the chosen preset goes on stage.
                        if (!selection.scope.isBatch) lookPresets.firstOrNull { it.name == name }?.let(viewModel::applyPreset)
                    }
                }
                item {
                    RenderFormatGroup(
                        destination = destination,
                        onDestination = { d ->
                            destinationName = d.name
                            options = store.loadFor(d)
                        },
                        options = options,
                        update = update,
                        gate = gate,
                    )
                }
                if (!selection.scope.isBatch) {
                    item {
                        RenderLengthGroup(
                            length = length,
                            onLength = { length = it },
                            trackDurationMs = player.durationMs,
                            loopLocked = !gate.allows(ProFeature.FULL_LENGTH_EXPORT),
                            onLoop = { loopOpen = true },
                        )
                    }
                    item {
                        RenderGroup(stringResource(R.string.render_group_save)) {
                            RenderChipRow {
                                RenderChip(stringResource(R.string.render_save_library), !askWhere) { askWhere = false }
                                RenderChip(stringResource(R.string.render_save_ask), askWhere) { askWhere = true }
                            }
                        }
                    }
                }
            }
        }
        RenderBar(
            export = export,
            scope = { modifier ->
                Text(
                    stringResource(
                        R.string.render_summary,
                        exportQualityLabel(options.quality),
                        fpsLabels()[EXPORT_FPS_OPTIONS.indexOf(options.fps).coerceAtLeast(0)],
                        stringResource(if (askWhere) R.string.render_save_ask else R.string.render_save_library),
                    ),
                    modifier = modifier,
                    style = MaterialTheme.typography.labelMedium,
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
                val aspect = ExportAspect.of(options.quality, options.ratio)
                val blocking = gate.blockingExportFeature(minOf(aspect.width, aspect.height), options.fps)
                if (blocking != null) proBlock = blocking else askKeepThenRender(options)
            },
            onCancel = studioViewModel::cancelExport,
            onClearResult = studioViewModel::resetExportState,
        )
    }
    if (loopOpen) ExportHost(viewModel, visualizerView) { loopOpen = false }
    proBlock?.let { feature ->
        RenderProChoiceDialog(
            onUnlock = {
                proBlock = null
                paywall.open(PaywallTrigger.Feature(feature))
            },
            onFreeVersion = {
                proBlock = null
                askKeepThenRender(options.freeVersion())
            },
            onDismiss = { proBlock = null },
        )
    }
    keepAsk?.let { chosen ->
        RenderKeepProjectDialog(
            onAnswer = { keep, dontAskAgain ->
                keepAsk = null
                if (dontAskAgain) studioPrefs.saveKeepAsProject(keep)
                renderOne(chosen, keep)
            },
            onDismiss = { keepAsk = null },
        )
    }
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

/** The same video inside the free limits: 4K drops to 1080p and the frame rate to the free cap. */
private fun ExportDefaults.freeVersion(): ExportDefaults =
    copy(
        quality = if (quality.shortSide > FreeTier.FREE_MAX_SHORT_SIDE) ExportQuality.FHD1080 else quality,
        fps = fps.coerceAtMost(FreeTier.FREE_MAX_FPS),
    )

/**
 * A free user pressed Render with a Pro choice (4K or 60 fps). Never a dead end: unlock Pro, or
 * render the same video at the free limits now (1080p, 30 fps, the first minute, the watermark).
 */
@Composable
private fun RenderProChoiceDialog(
    onUnlock: () -> Unit,
    onFreeVersion: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.render_pro_title)) },
        text = { Text(stringResource(R.string.render_pro_body)) },
        confirmButton = { TextButton(onClick = onUnlock) { Text(stringResource(R.string.render_pro_unlock)) } },
        dismissButton = { TextButton(onClick = onFreeVersion) { Text(stringResource(R.string.render_pro_free)) } },
    )
}

/**
 * "Add it to My videos?" before a single render starts. My videos lists renders by the project
 * that holds them, so a kept render becomes a project of the song and the look (never edited:
 * Studio has no editor); "don't ask again" stores the answer in [StudioPrefsStore]. Dismissing the
 * dialog renders nothing.
 */
@Composable
private fun RenderKeepProjectDialog(
    onAnswer: (keep: Boolean, remember: Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    var dontAsk by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.render_keep_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.render_keep_body))
                // The whole row toggles, so TalkBack reads the label with the box and the text is a target.
                Row(
                    Modifier
                        .fillMaxWidth()
                        .toggleable(value = dontAsk, role = Role.Checkbox, onValueChange = { dontAsk = it }),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Checkbox(checked = dontAsk, onCheckedChange = null)
                    Text(stringResource(R.string.render_keep_remember))
                }
            }
        },
        confirmButton = { TextButton(onClick = { onAnswer(true, dontAsk) }) { Text(stringResource(R.string.render_keep_yes)) } },
        dismissButton = { TextButton(onClick = { onAnswer(false, dontAsk) }) { Text(stringResource(R.string.render_keep_no)) } },
    )
}

/** Source › which recorded take replays on the song, or none (the live look and its reactions). */
@Composable
private fun RenderTakeChoice(
    takes: List<String>,
    selected: String?,
    onSelect: (String?) -> Unit,
) {
    OptionLabel(stringResource(R.string.export_group_performance))
    RenderChipRow {
        RenderChip(stringResource(R.string.export_live_settings), selected == null) { onSelect(null) }
        takes.forEach { name -> RenderChip(name, selected == name) { onSelect(name) } }
    }
}

/**
 * Format: a destination first (its own defaults from Settings › Export fill everything in), then
 * every option on its own. Choices beyond the free tier carry ♛ but stay selectable: Render offers
 * the paywall or a free version, so a free user can see what Pro would give.
 */
@Composable
private fun RenderFormatGroup(
    destination: ExportDestination?,
    onDestination: (ExportDestination) -> Unit,
    options: ExportDefaults,
    update: (ExportDefaults) -> Unit,
    gate: FeatureGate,
) {
    // One ♛ for the group: 4K and frame rates above the free cap stay selectable (Render then offers
    // the paywall or a free version), so the mark says "something here is Pro", not which chip.
    val proInside = !gate.allows(ProFeature.EXPORT_4K) || !gate.allows(ProFeature.EXPORT_60FPS)
    val title = stringResource(R.string.render_group_format) + if (proInside) stringResource(R.string.render_pro_mark) else ""
    RenderGroup(title) {
        OptionLabel(stringResource(R.string.render_destination))
        RenderChipRow {
            ExportDestination.entries.forEach { d ->
                val label = d.preset?.let { stringResource(it.nameRes) } ?: stringResource(R.string.render_destination_custom)
                RenderChip(label, destination == d) { onDestination(d) }
            }
        }
        RenderVideoOptions(options, update)
        RenderSoundOptions(options, update)
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
 * The look a render uses: the current one (null) or a preset. For one song the preset is put on
 * stage, so the video is what the user sees; a batch uses it for every track. Presets that draw
 * through a custom shader or a Milkdrop file are left out: the offscreen export renders a scene id
 * and its parameters, and those two need the live view's own shader state.
 */
@Composable
private fun RenderLookGroup(
    presets: List<Preset>,
    lookName: String?,
    onLook: (String?) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    RenderGroup(stringResource(R.string.render_group_look)) {
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

/** Format's picture options: quality, frame rate, ratio and codec. */
@Composable
private fun RenderVideoOptions(
    options: ExportDefaults,
    update: (ExportDefaults) -> Unit,
) {
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
    if (!hevcEncoder) OptionHint(stringResource(R.string.export_codec_no_hevc))
}

@Composable
private fun RenderSoundOptions(
    options: ExportDefaults,
    update: (ExportDefaults) -> Unit,
) {
    OptionLabel(stringResource(R.string.export_loudness_target))
    RenderChipRow {
        LoudnessTarget.ALL.forEach { target ->
            RenderChip(stringResource(target.labelRes), options.loudnessTargetId == target.id) {
                update(options.copy(loudnessTargetId = target.id))
            }
        }
    }
}

/**
 * Length: the whole song, a part of it, or a loop. Loop is the long-form render (a short seamless
 * loop repeated for up to hours), Pro, and keeps its own options in [LoopRenderSheet].
 */
@Composable
private fun RenderLengthGroup(
    length: RenderLength,
    onLength: (RenderLength) -> Unit,
    trackDurationMs: Long,
    loopLocked: Boolean,
    onLoop: () -> Unit,
) {
    RenderGroup(stringResource(R.string.export_length)) {
        RenderChipRow {
            if (trackDurationMs > 0) {
                RenderChip(stringResource(R.string.export_whole_track), !length.segment) { onLength(length.copy(segment = false)) }
                RenderChip(stringResource(R.string.export_segment), length.segment) { onLength(length.copy(segment = true)) }
            }
            val crown = if (loopLocked) stringResource(R.string.render_pro_mark) else ""
            RenderChip(stringResource(R.string.render_length_loop) + crown, selected = false, onClick = onLoop)
        }
        if (trackDurationMs > 0) {
            if (length.segment) {
                RangeSlider(
                    value = length.start..length.end,
                    onValueChange = { r -> onLength(length.copy(start = r.start, end = r.endInclusive)) },
                )
                val range = length.rangeFor(trackDurationMs)
                OptionHint(
                    if (range == null) {
                        stringResource(R.string.export_segment_hint, (ExportRange.minDurationMs / 1000).toInt())
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
    }
}

/**
 * A Render section: a Title Small heading on plain floating glass, the owner's look (no overline,
 * no glow). Settings keeps its own groups until 3B restyles them.
 */
@Composable
private fun RenderGroup(
    title: String,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        Modifier
            .fillMaxWidth()
            .floatingGlass()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        ProText(title, Modifier.semantics { heading() }, style = MaterialTheme.typography.titleSmall)
        content()
    }
}

/** A label that may carry ♛. TalkBack reads ♛ as "black chess queen", so it says "Pro" instead. */
@Composable
internal fun ProText(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = LocalTextStyle.current,
) {
    val pro = stringResource(R.string.studio_pro_spoken)
    val spoken = if ('♛' in text) modifier.semantics { contentDescription = text.replace("♛", pro) } else modifier
    Text(text, spoken, style = style)
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
        horizontalArrangement = Arrangement.spacedBy(8.dp),
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
        label = { ProText(label) },
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
