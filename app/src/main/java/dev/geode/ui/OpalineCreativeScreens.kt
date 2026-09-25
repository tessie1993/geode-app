package dev.geode.ui

import android.graphics.Rect
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.nav.Destination
import dev.geode.nav.Gate
import dev.geode.nav.Navigator
import dev.geode.nav.Overlay
import dev.geode.render.VisualizerView
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.opaline.OpalineButton
import dev.geode.ui.opaline.OpalineColors
import dev.geode.ui.opaline.OpalineIconButton
import dev.geode.ui.opaline.OpalinePage
import dev.geode.ui.opaline.OpalinePanel
import dev.geode.ui.opaline.OpalineRow
import dev.geode.ui.opaline.OpalineSlider
import dev.geode.ui.opaline.OpalineToggle
import dev.geode.ui.opaline.creative.CreativeTabs
import dev.geode.ui.opaline.opalinePart
import dev.geode.ui.studio.TimelineEditor
import kotlin.math.roundToInt

/** Destination-bound creative workspace. Each page retains its place through the Navigator. */
@Composable
fun OpalineCreativeRoute(
    destination: Destination,
    navigator: Navigator,
    player: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val settings: SettingsViewModel = geodeViewModel()
    val studio: StudioViewModel = geodeViewModel()
    val back: () -> Unit = { navigator.back() }
    when (destination) {
        Destination.Visuals.Hub -> CreativeVisualsHome(player, visualizerView, navigator)
        Destination.Visuals.Customize ->
            OpalinePage(stringResource(R.string.oc_customize), onBack = back) {
                CustomizePanel(player, visualizerView)
            }
        Destination.Visuals.Presets ->
            OpalinePage(
                stringResource(R.string.oc_presets),
                onBack = back,
            ) { PresetsTreeTab(player, visualizerView) }
        Destination.Visuals.Modulation ->
            OpalinePage(stringResource(R.string.oc_modulation), stringResource(R.string.oc_modulation_subtitle), back) {
                CreativeScroll { ParamPage(player, visualizerView, CustomizeTab.REACTIVITY) }
            }
        Destination.Visuals.Palette ->
            OpalinePage(stringResource(R.string.oc_palette), onBack = back) {
                CreativeScroll { ParamPage(player, visualizerView, CustomizeTab.COLOR) }
            }
        Destination.Visuals.ShaderEditor ->
            OpalinePage(stringResource(R.string.oc_shader), onBack = back) {
                CreativeScroll { GlslHubTab(player, visualizerView) }
            }
        Destination.Visuals.Background -> BackgroundSheet(back)
        Destination.Visuals.Layers -> {
            val art by player.overlayOptions.collectAsStateWithLifecycle()
            val watermark by player.watermarkOptions.collectAsStateWithLifecycle()
            val lyrics by player.overlayLyricOptions.collectAsStateWithLifecycle()
            LayersSheet(
                art,
                { next -> player.setOverlayOptions { next } },
                watermark,
                { next -> player.setWatermarkOptions { next } },
                player::pickWatermarkImage,
                player::clearWatermarkImage,
                lyrics,
                { next -> player.setOverlayLyricOptions { next } },
                back,
            )
        }
        Destination.Studio.Projects -> CreativeStudioHome(studio, navigator)
        is Destination.Studio.Editor -> {
            val editor by studio.editor.collectAsStateWithLifecycle()
            val state by studio.studio.collectAsStateWithLifecycle()
            LaunchedEffect(destination.projectId) { studio.openProject(destination.projectId) }
            if (editor.loaded && editor.name == destination.projectId) {
                TimelineEditor(editor, state.phase, studio, back)
            } else {
                OpalinePage(destination.projectId, onBack = back) {
                    OpalinePanel { Text(stringResource(R.string.oc_projects), style = MaterialTheme.typography.bodyMedium) }
                }
            }
        }
        Destination.Studio.Templates -> TemplatesSheet(player, visualizerView, back)
        Destination.Studio.LoopRender -> ExportHost(player, visualizerView, back, initialLoop = true)
        Destination.Settings.Root -> CreativeSettingsHome(navigator)
        Destination.Settings.Playback ->
            OpalinePage(stringResource(R.string.oc_playback), onBack = back) {
                CreativeScroll { SettingsGroup(stringResource(R.string.oc_playback)) { PlaybackSettingsSection(settings) } }
            }
        Destination.Settings.Audio -> OpalinePage(stringResource(R.string.oc_audio), onBack = back) { AudioSettingsTab(player) }
        Destination.Settings.Look -> CreativeLookSettings(settings, back)
        Destination.Settings.Behavior -> OpalinePage(stringResource(R.string.oc_behavior), onBack = back) { BehaviorSettingsTab(settings) }
        Destination.Settings.Folders -> OpalinePage(stringResource(R.string.oc_folders), onBack = back) { FolderSettingsTab() }
        Destination.Settings.ExternalAudio ->
            OpalinePage(stringResource(R.string.oc_external), onBack = back) {
                CreativeScroll {
                    SettingsGroup(stringResource(R.string.oc_external)) { ExternalAudioSettings(player) }
                    LiveInputGroup(player)
                }
            }
        Destination.Settings.AutoVisuals ->
            OpalinePage(stringResource(R.string.oc_auto), onBack = back) {
                CreativeScroll { SettingsGroup(stringResource(R.string.oc_auto)) { AutoVisualsGroup(player) } }
            }
        Destination.Settings.Export ->
            OpalinePage(stringResource(R.string.oc_export), onBack = back) {
                ExportSettingsTab(false) { navigator.open(Overlay.Export) }
            }
        Destination.Settings.Help ->
            OpalinePage(stringResource(R.string.oc_help), onBack = back) {
                HelpSettingsTab(settings) { navigator.raiseGate(Gate.TUTORIAL) }
            }
        Destination.Settings.About -> OpalinePage(stringResource(R.string.oc_about), onBack = back) { AboutSettingsTab() }
        else -> Unit // Player, Library and shared destinations belong to the shell.
    }
}

@Composable
fun OpalineExportOverlay(
    player: PlayerViewModel,
    visualizerView: VisualizerView,
    navigator: Navigator,
) {
    ExportHost(player, visualizerView, { navigator.close(Overlay.Export) })
}

/** Full-screen, engine-backed visualizer with accessible playback and close controls. */
@Composable
fun OpalineImmersive(
    player: PlayerViewModel,
    renderer: VisualizerView,
    navigator: Navigator,
    secondScreenName: String?,
) {
    val playback by player.uiState.collectAsStateWithLifecycle()
    DisposableEffect(renderer, secondScreenName) {
        VisualizerPipCoordinator.visualizerShowing = secondScreenName == null
        VisualizerPipCoordinator.canvasBoundsPx = null
        onDispose {
            VisualizerPipCoordinator.visualizerShowing = false
            VisualizerPipCoordinator.canvasBoundsPx = null
        }
    }
    Box(Modifier.fillMaxSize().background(Color.Black)) {
        if (secondScreenName == null) {
            AndroidView(
                factory = { renderer },
                modifier =
                    Modifier.fillMaxSize().onGloballyPositioned { coordinates ->
                        val bounds = coordinates.boundsInWindow()
                        VisualizerPipCoordinator.canvasBoundsPx =
                            Rect(
                                bounds.left.roundToInt(),
                                bounds.top.roundToInt(),
                                bounds.right.roundToInt(),
                                bounds.bottom.roundToInt(),
                            )
                    },
            )
        } else {
            Text(secondScreenName, Modifier.align(Alignment.Center), color = Color.White, style = MaterialTheme.typography.titleLarge)
        }
        Row(
            Modifier.align(Alignment.TopEnd).safeDrawingPadding().padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (!secondScreenName.isNullOrBlank()) {
                Text(secondScreenName, style = MaterialTheme.typography.labelMedium, color = Color.White)
            }
            OpalineIconButton(Icons.Filled.Close, stringResource(R.string.action_close), { navigator.close(Overlay.Visualizer) })
        }
        Row(
            Modifier.align(Alignment.BottomCenter).safeDrawingPadding().padding(bottom = 24.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OpalineIconButton(
                Icons.Filled.SkipPrevious,
                stringResource(R.string.action_previous),
                player::previous,
                enabled = playback.hasMedia,
            )
            OpalineButton(
                stringResource(if (playback.isPlaying) R.string.action_pause else R.string.action_play),
                player::togglePlayPause,
                icon = if (playback.isPlaying) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                enabled = playback.hasMedia,
            )
            OpalineIconButton(Icons.Filled.SkipNext, stringResource(R.string.action_next), player::next, enabled = playback.hasMedia)
        }
    }
}

@Composable
private fun CreativeVisualsHome(
    player: PlayerViewModel,
    visualizerView: VisualizerView,
    navigator: Navigator,
) {
    val viz by player.vizState.collectAsStateWithLifecycle()
    var workspace by rememberSaveable { mutableIntStateOf(0) }
    OpalinePage(stringResource(R.string.nav_visuals), stringResource(R.string.oc_visuals_subtitle), actions = {
        OpalineIconButton(Icons.Filled.PlayArrow, stringResource(R.string.oc_live), { navigator.open(Overlay.Visualizer) })
    }) {
        OpalinePanel(Modifier.padding(horizontal = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Box(Modifier.size(66.dp).opalinePart("B13", value = viz.params.motionAmount), contentAlignment = Alignment.Center) {
                    Icon(Icons.Filled.AutoAwesome, null, tint = OpalineColors.ink)
                }
                Column(Modifier.weight(1f)) {
                    Text(
                        stringResource(R.string.oc_current_scene),
                        style = MaterialTheme.typography.labelSmall,
                        color = OpalineColors.muted,
                    )
                    Text(sceneDisplayLabel(viz.sceneId), style = MaterialTheme.typography.titleLarge)
                }
                OpalineIconButton(Icons.Filled.Tune, stringResource(R.string.oc_customize), { navigator.go(Destination.Visuals.Customize) })
            }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OpalineButton(stringResource(R.string.oc_presets), { navigator.go(Destination.Visuals.Presets) })
                OpalineButton(
                    stringResource(R.string.oc_palette),
                    { navigator.go(Destination.Visuals.Palette) },
                    icon = Icons.Filled.Palette,
                )
                OpalineButton(
                    stringResource(R.string.oc_modulation),
                    { navigator.go(Destination.Visuals.Modulation) },
                    icon = Icons.Filled.GraphicEq,
                )
                OpalineButton(stringResource(R.string.oc_layers), { navigator.go(Destination.Visuals.Layers) }, icon = Icons.Filled.Layers)
                OpalineButton(stringResource(R.string.oc_background), { navigator.go(Destination.Visuals.Background) })
                OpalineButton(stringResource(R.string.oc_shader), { navigator.go(Destination.Visuals.ShaderEditor) })
            }
        }
        CreativeTabs(listOf(stringResource(R.string.oc_scenes), stringResource(R.string.oc_textures)), workspace, {
            workspace = it
        }, Modifier.padding(horizontal = 16.dp))
        if (workspace == 1) {
            TexturesHubTab(player, visualizerView)
        } else {
            SceneGallery(player, visualizerView) { workspace = 1 }
        }
    }
}

@Composable
private fun SceneGallery(
    player: PlayerViewModel,
    visualizerView: VisualizerView,
    openTextures: () -> Unit,
) {
    val viz by player.vizState.collectAsStateWithLifecycle()
    var family by rememberSaveable { mutableIntStateOf(0) }
    val families = listOf("Silk", "Life", "Mycelium", "Acid", "Shaders", "Fluid", "Cymatics", "MilkDrop")
    CreativeTabs(families, family, { family = it }, Modifier.padding(horizontal = 16.dp))
    if (family == 7) {
        MilkDropTab(player, visualizerView, openTextures)
        return
    }
    val ids =
        when (family) {
            0 -> VisualStyleCatalog.silkIds
            1 -> VisualStyleCatalog.lifeIds
            2 -> VisualStyleCatalog.mycoIds
            3 -> VisualStyleCatalog.acidIds
            4 -> SceneCapabilities.SHADER_SCENES.keys.toList()
            5 -> VisualStyleCatalog.fluidIds + listOf(SceneIds.CURLFLOW, SceneIds.WATER)
            else -> VisualStyleCatalog.cymaticsIds
        }
    LazyVerticalGrid(
        GridCells.Adaptive(144.dp),
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        items(ids, key = { it }) { id ->
            val active = id == viz.sceneId
            Column(
                Modifier
                    .fillMaxWidth()
                    .height(138.dp)
                    .opalinePart("C01", selected = active)
                    .background(OpalineColors.deep.copy(alpha = 0.64f), RoundedCornerShape(24.dp))
                    .semantics { selected = active }
                    .clickable(role = Role.RadioButton) { player.selectScene(id) }
                    .padding(18.dp),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Icon(
                        Icons.Filled.AutoAwesome,
                        null,
                        Modifier.size(28.dp),
                        tint = if (active) OpalineColors.accent else OpalineColors.lavender,
                    )
                    if (active) {
                        Icon(Icons.Filled.Check, stringResource(R.string.oc_selected), tint = OpalineColors.accent)
                    }
                }
                Text(sceneDisplayLabel(id), style = MaterialTheme.typography.titleSmall)
            }
        }
    }
}

@Composable
private fun ParamPage(
    player: PlayerViewModel,
    visualizerView: VisualizerView,
    tab: CustomizeTab,
) {
    val viz by player.vizState.collectAsStateWithLifecycle()
    ParamSurface(sceneId = viz.sceneId, query = "") { CustomizeTabBody(player, visualizerView, tab) }
}

@Composable
private fun CreativeStudioHome(
    studio: StudioViewModel,
    navigator: Navigator,
) {
    val editor by studio.editor.collectAsStateWithLifecycle()
    val takes by studio.takeState.collectAsStateWithLifecycle()
    val projects by studio.projectNames.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var creating by rememberSaveable { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var failed by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(Unit) { studio.refreshProjects() }
    OpalinePage(stringResource(R.string.nav_studio), stringResource(R.string.oc_studio_subtitle), actions = {
        OpalineIconButton(Icons.Filled.PlayArrow, stringResource(R.string.oc_export), { navigator.open(Overlay.Export) })
    }) {
        CreativeTabs(
            listOf(
                stringResource(R.string.oc_projects),
                stringResource(R.string.oc_media),
                stringResource(R.string.oc_takes),
            ),
            tab,
            {
                tab =
                    it
            },
            Modifier.padding(horizontal = 16.dp),
        )
        when (tab) {
            1 -> StudioRoute(studio)
            2 -> TakesTab(studio)
            else ->
                CreativeScroll {
                    OpalinePanel {
                        Text(stringResource(R.string.oc_timeline), style = MaterialTheme.typography.headlineMedium)
                        Text(editor.name, style = MaterialTheme.typography.titleMedium, color = OpalineColors.lavender)
                        Text(
                            stringResource(
                                R.string.oc_project_detail,
                                editor.project.timeline.lanes.size,
                                editor.project.timeline.durationMs / 1000,
                            ),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        OpalineButton(stringResource(R.string.oc_open_timeline), {
                            navigator.go(Destination.Studio.Editor(editor.name))
                        }, Modifier.fillMaxWidth(), Icons.Filled.Tune)
                    }
                    OpalineButton(stringResource(R.string.oc_new_project), {
                        creating = true
                        failed = false
                    })
                    projects.filter { it != editor.name }.forEach { project ->
                        OpalineRow(project, stringResource(R.string.oc_saved_project), { navigator.go(Destination.Studio.Editor(project)) })
                    }
                    OpalineRow(stringResource(R.string.oc_templates), stringResource(R.string.oc_templates_subtitle), {
                        navigator.go(Destination.Studio.Templates)
                    })
                    OpalineRow(
                        stringResource(R.string.oc_loop),
                        stringResource(R.string.oc_loop_subtitle),
                        { navigator.go(Destination.Studio.LoopRender) },
                    )
                    OpalinePanel {
                        Text(stringResource(R.string.oc_performance), style = MaterialTheme.typography.titleMedium)
                        Text(
                            stringResource(if (takes.recording) R.string.oc_recording else R.string.oc_performance_subtitle),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        OpalineButton(stringResource(if (takes.recording) R.string.oc_stop_recording else R.string.oc_record), {
                            if (takes.recording) studio.stopRecording() else studio.startRecording()
                        }, selected = takes.recording)
                    }
                }
        }
    }
    if (creating) {
        AlertDialog(
            onDismissRequest = { creating = false },
            title = { Text(stringResource(R.string.oc_new_project)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedTextField(name, {
                        name = it
                        failed = false
                    }, singleLine = true, label = { Text(stringResource(R.string.oc_project_name)) })
                    if (failed) Text(stringResource(R.string.oc_project_failed), color = MaterialTheme.colorScheme.error)
                }
            },
            confirmButton = {
                OpalineButton(stringResource(R.string.oc_create), {
                    studio.createProject(name) { created ->
                        failed = !created
                        if (created) {
                            creating = false
                            navigator.go(Destination.Studio.Editor(name.trim()))
                            name = ""
                        }
                    }
                }, enabled = name.isNotBlank())
            },
            dismissButton = { OpalineButton(stringResource(R.string.action_cancel), { creating = false }) },
        )
    }
}

@Composable
private fun CreativeSettingsHome(navigator: Navigator) {
    val rows =
        listOf(
            Triple(R.string.oc_playback, R.string.oc_playback_subtitle, Destination.Settings.Playback),
            Triple(R.string.oc_audio, R.string.oc_audio_subtitle, Destination.Settings.Audio),
            Triple(R.string.oc_look, R.string.oc_look_subtitle, Destination.Settings.Look),
            Triple(R.string.oc_behavior, R.string.oc_behavior_subtitle, Destination.Settings.Behavior),
            Triple(R.string.oc_folders, R.string.oc_folders_subtitle, Destination.Settings.Folders),
            Triple(R.string.oc_external, R.string.oc_external_subtitle, Destination.Settings.ExternalAudio),
            Triple(R.string.oc_auto, R.string.oc_auto_subtitle, Destination.Settings.AutoVisuals),
            Triple(R.string.oc_export, R.string.oc_export_subtitle, Destination.Settings.Export),
            Triple(R.string.oc_help, R.string.oc_help_subtitle, Destination.Settings.Help),
            Triple(R.string.oc_about, R.string.oc_about_subtitle, Destination.Settings.About),
        )
    OpalinePage(stringResource(R.string.nav_settings), stringResource(R.string.oc_settings_subtitle)) {
        LazyColumn(contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(rows.size) { index ->
                val (title, subtitle, route) = rows[index]
                OpalineRow(stringResource(title), stringResource(subtitle), { navigator.go(route) }, trailing = {
                    Icon(Icons.AutoMirrored.Filled.ArrowForward, null, tint = OpalineColors.muted)
                })
            }
        }
    }
}

@Composable
private fun CreativeLookSettings(
    settings: SettingsViewModel,
    back: () -> Unit,
) {
    val gui by settings.guiPrefs.collectAsStateWithLifecycle()
    OpalinePage(stringResource(R.string.oc_look), stringResource(R.string.oc_look_subtitle), back) {
        CreativeScroll {
            SettingsGroup(stringResource(R.string.oc_motion)) {
                OpalineRow(stringResource(R.string.oc_reduced_motion), stringResource(R.string.oc_reduced_motion_subtitle), trailing = {
                    OpalineToggle(gui.reducedMotion, { settings.setGuiPrefs(gui.copy(reducedMotion = it)) })
                })
                Text(stringResource(R.string.oc_motion_amount))
                OpalineSlider(
                    gui.liquidMotion,
                    { settings.setGuiPrefs(gui.copy(liquidMotion = it)) },
                    range = 0f..1.5f,
                    enabled = !gui.reducedMotion,
                )
            }
            SettingsGroup(stringResource(R.string.oc_readability)) {
                Text(stringResource(R.string.oc_text_size, (gui.textScale * 100).toInt()))
                OpalineSlider(
                    gui.textScale,
                    { settings.setGuiPrefs(gui.copy(textScale = it)) },
                    range =
                        GuiPrefs.TEXT_SCALE_MIN..GuiPrefs.TEXT_SCALE_MAX,
                )
                Text(stringResource(R.string.oc_background_dim))
                OpalineSlider(gui.backgroundDim, { settings.setGuiPrefs(gui.copy(backgroundDim = it)) })
                Text(stringResource(R.string.oc_material_tint))
                OpalineSlider(gui.glassTint, { settings.setGuiPrefs(gui.copy(glassTint = it)) })
            }
        }
    }
}

@Composable
internal fun CreativeScroll(content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
        content = content,
    )
}

@Composable
internal fun SettingsTabColumn(content: LazyListScope.() -> Unit) {
    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
        content = content,
    )
}

@Composable
internal fun SettingsGroup(
    title: String,
    modifier: Modifier = Modifier,
    header: (@Composable RowScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    OpalinePanel(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SettingsOverline(title, Modifier.weight(1f))
            header?.invoke(this)
        }
        content()
    }
}

@Composable
internal fun SettingsOverline(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(text, modifier, style = MaterialTheme.typography.titleMedium, color = OpalineColors.lavender)
}
