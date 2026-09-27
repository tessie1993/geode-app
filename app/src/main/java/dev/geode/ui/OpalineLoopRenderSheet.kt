package dev.geode.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.export.ExportAspect
import dev.geode.export.LoopSpec
import dev.geode.export.TimeOfDayDrift
import kotlin.math.roundToInt
import dev.geode.ui.opaline.creative.CreativeSlider as Slider
import dev.geode.ui.opaline.creative.CreativeToggle as Switch

/**
 * Picks the loop's length, seam and palette drift, then starts [ExportController.startLoopRender]
 * through the same destination flow (Videos library or a chosen folder) the other exports use.
 *
 * The visual source is always the currently loaded track's analysis — there is nothing else in
 * this build to loop. The soundtrack defaults to that same track; the clip picker lets it be
 * replaced with a longer mix so the finished file can run past the track's own length.
 */
@Composable
fun LoopRenderSheet(
    state: LoopUiState,
    onStart: (LoopRenderRequest) -> Unit,
    onStartToDestination: (LoopRenderRequest) -> Unit,
    onCancel: () -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val defaults = remember { ExportPrefsStore(GeodePrefsFiles(context).general).load() }

    var loopSeconds by rememberSaveable { mutableStateOf(LoopSpec.MIN_LOOP_MS / 1000f) }
    var crossfadeSeconds by rememberSaveable { mutableStateOf(LoopSpec.DEFAULT_CROSSFADE_MS / 1000f) }
    var driftEnabled by rememberSaveable { mutableStateOf(false) }
    var driftHueTurns by rememberSaveable { mutableStateOf(0.15f) }
    var driftStops by rememberSaveable { mutableStateOf(4f) }
    var audioClips by rememberSaveable { mutableStateOf<List<String>>(emptyList()) }

    val clipPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isEmpty()) return@rememberLauncherForActivityResult
            // The grant is read-only and per-URI; it must be taken now, while the picker's own
            // grant is still active, so the clip is still readable after a process death and
            // restore — a long-form render is exactly the case meant to survive one.
            uris.forEach { uri ->
                runCatching {
                    context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
            }
            audioClips = audioClips + uris.map { it.toString() }
        }

    fun buildRequest(): LoopRenderRequest =
        LoopRenderRequest(
            aspect = ExportAspect.of(defaults.quality, defaults.ratio),
            codec = defaults.codec,
            fps = defaults.fps,
            loopMs = (loopSeconds * 1000).toLong(),
            crossfadeMs = (crossfadeSeconds * 1000).toLong(),
            drift =
                if (driftEnabled) {
                    TimeOfDayDrift(hueTurns = driftHueTurns, warmth = 0f, stops = driftStops.roundToInt())
                } else {
                    TimeOfDayDrift.None
                },
            audioClips = audioClips.map { Uri.parse(it) },
        )

    OpalineContextSheet(onDismiss = onDismiss) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.export_loop_title), style = MaterialTheme.typography.titleLarge)
            when (val phase = state.phase) {
                is ExportPhase.Running -> LoopRenderRunning(phase.progress)
                is ExportPhase.Done -> LoopRenderDone(phase.resultUri)
                is ExportPhase.Failed ->
                    Text(
                        stringResource(R.string.export_failed, phase.message),
                        color = MaterialTheme.colorScheme.error,
                    )
                ExportPhase.Idle, ExportPhase.Loading ->
                    LoopRenderControls(
                        loopSeconds = loopSeconds,
                        onLoopSecondsChange = { loopSeconds = it },
                        crossfadeSeconds = crossfadeSeconds,
                        onCrossfadeSecondsChange = { crossfadeSeconds = it },
                        driftEnabled = driftEnabled,
                        onDriftEnabledChange = { driftEnabled = it },
                        driftHueTurns = driftHueTurns,
                        onDriftHueTurnsChange = { driftHueTurns = it },
                        driftStops = driftStops,
                        onDriftStopsChange = { driftStops = it },
                        audioClips = audioClips,
                        onAddClips = { clipPicker.launch(arrayOf("audio/*")) },
                        onRemoveClip = { clip -> audioClips = audioClips - clip },
                        onStart = { onStart(buildRequest()) },
                        onStartToDestination = { onStartToDestination(buildRequest()) },
                    )
            }

            if (state.phase.isRunning) {
                TextButton(onClick = onCancel) { Text(stringResource(R.string.export_cancel)) }
            }
        }
    }
}

@Composable
private fun LoopRenderRunning(progress: Float) {
    LinearProgressIndicator(
        progress = { progress },
        modifier = Modifier.fillMaxWidth(),
    )
    Text(
        stringResource(R.string.export_loop_rendering, (progress * 100).roundToInt()),
        style = MaterialTheme.typography.labelMedium,
    )
    Text(
        stringResource(R.string.export_leave_hint),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun LoopRenderDone(resultUri: Uri) {
    val context = LocalContext.current
    val chooserTitle = stringResource(R.string.export_upload_share_to)
    Text(
        stringResource(R.string.export_saved_library),
        style = MaterialTheme.typography.bodyMedium,
    )
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = {
            val share =
                Intent(Intent.ACTION_SEND).apply {
                    type = "video/mp4"
                    putExtra(Intent.EXTRA_STREAM, resultUri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
            context.startActivity(Intent.createChooser(share, chooserTitle))
        }) {
            Text(stringResource(R.string.export_upload_drive))
        }
    }
}

@Suppress("LongParameterList")
@Composable
private fun LoopRenderControls(
    loopSeconds: Float,
    onLoopSecondsChange: (Float) -> Unit,
    crossfadeSeconds: Float,
    onCrossfadeSecondsChange: (Float) -> Unit,
    driftEnabled: Boolean,
    onDriftEnabledChange: (Boolean) -> Unit,
    driftHueTurns: Float,
    onDriftHueTurnsChange: (Float) -> Unit,
    driftStops: Float,
    onDriftStopsChange: (Float) -> Unit,
    audioClips: List<String>,
    onAddClips: () -> Unit,
    onRemoveClip: (String) -> Unit,
    onStart: () -> Unit,
    onStartToDestination: () -> Unit,
) {
    Text(stringResource(R.string.export_loop_source_hint), style = MaterialTheme.typography.bodySmall)
    Text(
        stringResource(R.string.export_loop_length, loopSeconds.roundToInt()),
        style = MaterialTheme.typography.labelMedium,
    )
    Slider(
        value = loopSeconds,
        onValueChange = onLoopSecondsChange,
        valueRange = (LoopSpec.MIN_LOOP_MS / 1000f)..(LoopSpec.MAX_LOOP_MS / 1000f),
    )
    Text(
        stringResource(R.string.export_loop_crossfade, crossfadeSeconds),
        style = MaterialTheme.typography.labelMedium,
    )
    Slider(
        value = crossfadeSeconds,
        onValueChange = onCrossfadeSecondsChange,
        valueRange = (LoopSpec.MIN_CROSSFADE_MS / 1000f)..(LoopSpec.MAX_CROSSFADE_MS / 1000f),
    )
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.export_loop_drift_toggle), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Switch(checked = driftEnabled, onCheckedChange = onDriftEnabledChange)
    }
    if (driftEnabled) {
        Text(
            stringResource(R.string.export_loop_drift_hue, (driftHueTurns * 360).roundToInt()),
            style = MaterialTheme.typography.labelMedium,
        )
        Slider(value = driftHueTurns, onValueChange = onDriftHueTurnsChange, valueRange = -0.5f..0.5f)
        Text(
            stringResource(R.string.export_loop_drift_stops, driftStops.roundToInt()),
            style = MaterialTheme.typography.labelMedium,
        )
        Slider(
            value = driftStops,
            onValueChange = onDriftStopsChange,
            valueRange = 2f..TimeOfDayDrift.MAX_STOPS.toFloat(),
            steps = TimeOfDayDrift.MAX_STOPS - 3,
        )
    }
    Text(stringResource(R.string.export_loop_soundtrack_title), style = MaterialTheme.typography.labelMedium)
    Text(
        if (audioClips.isEmpty()) {
            stringResource(R.string.export_loop_soundtrack_empty)
        } else {
            stringResource(
                R.string.export_loop_soundtrack_count,
                pluralStringResource(R.plurals.clip_count, audioClips.size, audioClips.size),
            )
        },
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    audioClips.forEach { clip ->
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                clip.substringAfterLast('/').substringAfterLast(':'),
                Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodySmall,
            )
            TextButton(onClick = { onRemoveClip(clip) }) { Text(stringResource(R.string.action_delete)) }
        }
    }
    OutlinedButton(onClick = onAddClips, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.export_loop_soundtrack_add))
    }
    Button(onClick = onStart, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.export_loop_start))
    }
    OutlinedButton(onClick = onStartToDestination, modifier = Modifier.fillMaxWidth()) {
        Text(stringResource(R.string.export_render_to_folder))
    }
}
