package dev.geode.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.exportCodecLabel
import dev.geode.data.exportQualityLabel
import dev.geode.export.ExportCodec
import dev.geode.export.ExportPresets
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.pro.ProGate

/**
 * The Studio destination: Edit (the Pro clip editor) and Render (visualizer export, loop render
 * and the export defaults). Studio is the one owner of everything that makes a video; Settings
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
            titles = listOf(stringResource(R.string.studio_edit), stringResource(R.string.studio_render)),
            selected = section.ordinal,
            onSelect = { onSection(StudioSection.entries[it]) },
        )
        when (section) {
            StudioSection.EDIT -> ProGate(ProFeature.STUDIO) { StudioRoute() }
            StudioSection.RENDER -> StudioRenderTab(viewModel, visualizerView)
        }
    }
}

/** Studio › Render: the two kinds of render, then the defaults every render starts from. */
@Composable
internal fun StudioRenderTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    var open by rememberSaveable { mutableStateOf<ExportEntryMode?>(null) }
    val context = LocalContext.current
    val store = remember { ExportPrefsStore(GeodePrefsFiles(context).general) }
    var defaults by remember { mutableStateOf(store.load()) }
    // The export dialog edits the same stored defaults ("remember these"), so re-read them when it closes.
    LaunchedEffect(open) { if (open == null) defaults = store.load() }
    val update: (ExportDefaults) -> Unit = {
        defaults = it
        store.save(it)
    }
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.studio_render)) {
                Text(
                    stringResource(R.string.export_loop_menu_subtitle),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    CrystalButton(onClick = { open = ExportEntryMode.Standard }) {
                        Text(stringResource(R.string.export_loop_menu_standard))
                    }
                    CrystalButton(filled = false, onClick = { open = ExportEntryMode.Loop }) {
                        Text(stringResource(R.string.export_loop_menu_loop))
                    }
                    if (!gate.allows(ProFeature.FULL_LENGTH_EXPORT)) ProBadge()
                }
                if (!gate.isPro) {
                    Text(
                        stringResource(R.string.studio_render_free_limits),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        exportDefaultsItems(defaults, update)
    }
    open?.let { mode ->
        ExportHost(viewModel, visualizerView, requested = mode) { open = null }
    }
}

private fun LazyListScope.exportDefaultsItems(
    defaults: ExportDefaults,
    update: (ExportDefaults) -> Unit,
) {
    item {
        SettingsGroup(stringResource(R.string.export_group_defaults)) {
            Column {
                Text(stringResource(R.string.export_platform_preset), style = MaterialTheme.typography.labelMedium)
                Row(Modifier.fillMaxWidth().padding(top = 4.dp).horizontalScroll(rememberScrollState())) {
                    CrystalSegmented(
                        options = ExportPresets.ALL.map { stringResource(it.nameRes) },
                        selected =
                            ExportPresets.indexMatching(
                                defaults.quality,
                                defaults.ratio,
                                defaults.fps,
                                defaults.loopSafe,
                            ),
                        onSelect = {
                            val preset = ExportPresets.ALL[it]
                            update(
                                defaults.copy(
                                    quality = preset.quality,
                                    ratio = preset.ratio,
                                    fps = preset.fps,
                                    loopSafe = preset.loopSafe,
                                ),
                            )
                        },
                    )
                }
                Text(
                    presetCaption(
                        defaults,
                        stringResource(R.string.export_spec, defaults.ratio.label, exportQualityLabel(defaults.quality), defaults.fps),
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Column {
                Text(stringResource(R.string.export_quality), style = MaterialTheme.typography.labelMedium)
                CrystalSegmented(
                    options = ExportQuality.entries.map { exportQualityLabel(it) },
                    selected = ExportQuality.entries.indexOf(defaults.quality),
                    onSelect = { update(defaults.copy(quality = ExportQuality.entries[it])) },
                    modifier = Modifier.padding(top = 4.dp),
                )
            }
            Column {
                Text(stringResource(R.string.export_frame_rate), style = MaterialTheme.typography.labelMedium)
                CrystalSegmented(
                    options = fpsLabels(),
                    selected = EXPORT_FPS_OPTIONS.indexOf(defaults.fps).coerceAtLeast(0),
                    onSelect = { update(defaults.copy(fps = EXPORT_FPS_OPTIONS[it])) },
                    modifier = Modifier.padding(top = 4.dp),
                )
            }
            Column {
                Text(stringResource(R.string.export_aspect_ratio), style = MaterialTheme.typography.labelMedium)
                Row(Modifier.fillMaxWidth().padding(top = 4.dp).horizontalScroll(rememberScrollState())) {
                    CrystalSegmented(
                        options = ExportRatio.entries.map { it.label },
                        selected = ExportRatio.entries.indexOf(defaults.ratio),
                        onSelect = { update(defaults.copy(ratio = ExportRatio.entries[it])) },
                    )
                }
            }
            Column {
                Text(stringResource(R.string.export_codec), style = MaterialTheme.typography.labelMedium)
                CrystalSegmented(
                    options = ExportCodec.entries.map { exportCodecLabel(it) },
                    selected = ExportCodec.entries.indexOf(defaults.codec),
                    onSelect = { update(defaults.copy(codec = ExportCodec.entries[it])) },
                    modifier = Modifier.padding(top = 4.dp),
                )
                val hevcEncoder = remember { ExportCodec.hasEncoder(ExportCodec.HEVC.mimeType) }
                Text(
                    stringResource(if (hevcEncoder) R.string.export_codec_explainer else R.string.export_codec_no_hevc),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(R.string.export_loop_safe_default),
                        Modifier.weight(1f),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Switch(
                        checked = defaults.loopSafe,
                        onCheckedChange = { update(defaults.copy(loopSafe = it)) },
                    )
                }
                Text(
                    stringResource(R.string.export_loop_safe_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

// Shared by Studio › Render and the export dialog so the frame-rate segmented control reads
// identically ("24 fps", "25 fps", …) in both places.
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

@Composable
internal fun presetCaption(
    defaults: ExportDefaults,
    baseSpec: String,
): String {
    val spec =
        if (defaults.loopSafe) stringResource(R.string.export_spec_loop_safe, baseSpec) else baseSpec
    return ExportPresets
        .matching(defaults.quality, defaults.ratio, defaults.fps, defaults.loopSafe)
        ?.let { stringResource(R.string.export_named_preset, stringResource(it.nameRes), spec) }
        ?: stringResource(R.string.export_custom_preset, spec)
}
