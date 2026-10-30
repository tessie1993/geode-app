package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RangeSlider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.analysis.BarTrim
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.exportCodecLabel
import dev.geode.data.exportQualityLabel
import dev.geode.data.filledFrom
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRange
import dev.geode.export.ExportRatio
import dev.geode.export.LoudnessTarget

/**
 * The visualizer export: preset, quality, frame rate, codec, loudness, ratio and length for one
 * render. Opened from Studio › Render through [ExportHost]; it used to be named `SettingsDialog`,
 * which is how export came to look like a Settings feature.
 */
@Composable
fun ExportDialog(
    export: ExportUiState,
    hasMedia: Boolean,
    takes: List<String>,
    selectedTake: String?,
    onSelectTake: (String?) -> Unit,
    bpm: Float,
    trackDurationMs: Long,
    onStart: (ExportAspect, Int, Boolean, ExportRange?, ExportCodec) -> Unit,
    onStartToDestination: (ExportAspect, Int, Boolean, ExportRange?, ExportCodec) -> Unit,
    onCancel: () -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val exportPrefs = remember { ExportPrefsStore(GeodePrefsFiles(context).general) }
    val defaults = remember { exportPrefs.load() }
    var quality by rememberSaveable { mutableStateOf(defaults.quality) }
    var ratio by rememberSaveable { mutableStateOf(defaults.ratio) }
    var fps by rememberSaveable { mutableStateOf(defaults.fps) }
    var codec by rememberSaveable { mutableStateOf(defaults.codec) }
    // LoudnessTarget is a sealed interface, not a Saveable type on its own, so the persisted
    // choice is carried as its id and resolved back through LoudnessTarget.byId.
    var loudnessTargetId by rememberSaveable { mutableStateOf(defaults.loudnessTargetId) }
    val loudnessTarget = LoudnessTarget.byId(loudnessTargetId)
    var loopSafe by remember {
        mutableStateOf(
            defaults.loopSafe &&
                dev.geode.analysis.BarTrim
                    .barDurationUs(bpm) != null,
        )
    }
    var segment by rememberSaveable { mutableStateOf(false) }
    var rangeStart by remember { mutableFloatStateOf(0f) }
    var rangeEnd by remember { mutableFloatStateOf(1f) }
    val range =
        if (!segment) {
            null
        } else {
            ExportRange.of(
                startMs = (rangeStart * trackDurationMs).toLong(),
                endMs = (rangeEnd * trackDurationMs).toLong(),
                trackDurationMs = trackDurationMs,
            )
        }

    fun persistDefaults() = exportPrefs.save(ExportDefaults(quality, fps, ratio, loopSafe, codec, loudnessTargetId))
    val chooserTitle = stringResource(R.string.export_upload_share_to)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.export_title)) },
        text = {
            // Scrolls: on a phone the options run past the dialog and hid the Start button.
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                when (val phase = export.phase) {
                    is ExportPhase.Running -> {
                        val run by dev.geode.export.ExportRun.state
                            .collectAsStateWithLifecycle()
                        Text(
                            listOfNotNull(
                                stringResource(R.string.export_rendering_offline),
                                run.secondsRemaining?.let {
                                    dev.geode.export.RenderEta
                                        .describe(LocalResources.current, it)
                                },
                            ).joinToString(" · "),
                        )
                        LinearProgressIndicator(
                            progress = { phase.progress },
                            modifier = Modifier.fillMaxWidth(),
                        )
                        Text(
                            stringResource(R.string.export_leave_hint),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    is ExportPhase.Done -> {
                        Text(
                            stringResource(
                                if (export.customDestination) {
                                    R.string.export_saved_folder
                                } else {
                                    R.string.export_saved_library
                                },
                            ),
                        )
                        export.loudnessAdvice?.let { advice ->
                            Text(advice.headline(LocalResources.current), style = MaterialTheme.typography.labelMedium)
                            Text(
                                advice.detail(LocalResources.current),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            // No track title reaches this dialog, so the rendered file's own
                            // name (e.g. "muzviz_1234567890.mp4") stands in for EXTRA_TITLE/SUBJECT.
                            val resultName = phase.resultUri.lastPathSegment?.substringAfterLast('/')
                            Button(onClick = {
                                context.shareVideo(phase.resultUri, chooserTitle, title = resultName, subject = resultName)
                            }) {
                                Text(stringResource(R.string.export_upload_drive))
                            }
                        }
                    }
                    is ExportPhase.Failed -> {
                        Text(stringResource(R.string.export_failed, phase.message), color = MaterialTheme.colorScheme.error)
                    }
                    ExportPhase.Idle, ExportPhase.Loading -> {
                        Text(stringResource(R.string.export_quality), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            ExportQuality.entries.forEach { q ->
                                QualityChip(exportQualityLabel(q), quality == q) {
                                    quality = q
                                    persistDefaults()
                                }
                            }
                        }
                        Text(stringResource(R.string.export_frame_rate), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            EXPORT_FPS_OPTIONS.zip(fpsLabels()).forEach { (option, label) ->
                                QualityChip(label, fps == option) {
                                    fps = option
                                    persistDefaults()
                                }
                            }
                        }
                        Text(stringResource(R.string.export_codec), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            ExportCodec.entries.forEach { c ->
                                QualityChip(exportCodecLabel(c), codec == c) {
                                    codec = c
                                    persistDefaults()
                                }
                            }
                        }
                        Text(stringResource(R.string.export_loudness_target), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            LoudnessTarget.ALL.forEach { target ->
                                QualityChip(stringResource(target.labelRes), loudnessTarget.id == target.id) {
                                    loudnessTargetId = target.id
                                    persistDefaults()
                                }
                            }
                        }
                        Text(
                            stringResource(R.string.export_loudness_hint),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(stringResource(R.string.export_aspect_ratio), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            ExportRatio.entries.forEach { r ->
                                QualityChip(r.label, ratio == r) {
                                    ratio = r
                                    persistDefaults()
                                }
                            }
                        }
                        PlatformFillMenu { preset ->
                            // The same one rule as Studio › Render (ExportDefaults.filledFrom).
                            val filled = ExportDefaults(quality, fps, ratio, loopSafe, codec, loudnessTargetId).filledFrom(preset)
                            quality = filled.quality
                            ratio = filled.ratio
                            fps = filled.fps
                            // A loop-safe cut needs the track's tempo; without one the chip below is off.
                            loopSafe = filled.loopSafe && BarTrim.barDurationUs(bpm) != null
                            persistDefaults()
                        }
                        if (trackDurationMs > 0) {
                            Text(stringResource(R.string.export_length), style = MaterialTheme.typography.labelMedium)
                            ChipRow {
                                QualityChip(stringResource(R.string.export_whole_track), !segment) { segment = false }
                                QualityChip(stringResource(R.string.export_segment), segment) { segment = true }
                            }
                            if (segment) {
                                RangeSlider(
                                    value = rangeStart..rangeEnd,
                                    onValueChange = { r ->
                                        rangeStart = r.start
                                        rangeEnd = r.endInclusive
                                    },
                                    modifier = Modifier.padding(top = 4.dp),
                                )
                                Text(
                                    if (range == null) {
                                        stringResource(
                                            R.string.export_segment_hint,
                                            (ExportRange.MIN_DURATION_MS / 1000).toInt(),
                                        )
                                    } else {
                                        stringResource(
                                            R.string.export_segment_summary,
                                            formatClock(range.startMs),
                                            formatClock(range.endMs),
                                            formatClock(range.durationMs),
                                        )
                                    },
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                        val barUs =
                            dev.geode.analysis.BarTrim
                                .barDurationUs(bpm)
                        Text(stringResource(R.string.export_looping), style = MaterialTheme.typography.labelMedium)
                        ChipRow {
                            QualityChip(stringResource(R.string.export_full_length), !loopSafe) {
                                loopSafe = false
                                persistDefaults()
                            }
                            QualityChip(stringResource(R.string.export_loop_safe), loopSafe, enabled = barUs != null) {
                                loopSafe = true
                                persistDefaults()
                            }
                        }
                        Text(
                            if (barUs != null) {
                                stringResource(
                                    R.string.export_loop_safe_bar_hint,
                                    "%.0f".format(bpm),
                                    "%.1f".format(barUs / 1_000_000f),
                                )
                            } else {
                                stringResource(R.string.export_loop_safe_needs_tempo)
                            },
                            style = MaterialTheme.typography.labelSmall,
                        )
                        if (takes.isNotEmpty()) {
                            Text(stringResource(R.string.export_group_performance), style = MaterialTheme.typography.labelMedium)
                            ChipRow {
                                QualityChip(stringResource(R.string.export_live_settings), selectedTake == null) { onSelectTake(null) }
                                takes.forEach { name ->
                                    QualityChip(name, selectedTake == name) { onSelectTake(name) }
                                }
                            }
                            if (selectedTake != null) {
                                Text(
                                    stringResource(R.string.export_take_explainer),
                                    style = MaterialTheme.typography.labelSmall,
                                )
                            }
                        }
                        if (quality == ExportQuality.UHD4K) {
                            Text(
                                stringResource(R.string.export_4k_fallback),
                                style = MaterialTheme.typography.labelSmall,
                            )
                        }
                        Button(
                            onClick = { onStart(ExportAspect.of(quality, ratio), fps, loopSafe, range, codec) },
                            enabled = hasMedia,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(stringResource(R.string.export_render_button, quality.shortSide, ratio.label, fps))
                        }
                        OutlinedButton(
                            onClick = { onStartToDestination(ExportAspect.of(quality, ratio), fps, loopSafe, range, codec) },
                            enabled = hasMedia,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(stringResource(R.string.export_render_to_folder))
                        }
                    }
                }
            }
        },
        confirmButton = {
            if (export.phase.isRunning) {
                TextButton(onClick = onCancel) { Text(stringResource(R.string.export_cancel)) }
            } else {
                TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) }
            }
        },
    )
}

/**
 * Every option row in this dialog wraps instead of scrolling sideways. A horizontally scrolled
 * row started at its left end, so on a phone the selected chip (YouTube, 60 fps, "Leave as-is")
 * sat off-screen while the caption above named it, and nothing hinted the row could scroll.
 */
@Composable
private fun ChipRow(content: @Composable () -> Unit) {
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) { content() }
}

@Composable
private fun QualityChip(
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
