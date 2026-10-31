package dev.geode.ui

import android.content.Context
import android.content.Intent
import android.os.Build
import android.provider.DocumentsContract
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.export.ExportRun
import dev.geode.export.RenderEta

/**
 * The bottom of Studio › Render: what a render will cover and the one Render button, then, once a
 * render is under way, its progress and Cancel, and once it is done, the file itself (Open, Share,
 * Show in folder). Before this the finished video was only named by a sentence in a dialog that
 * closed, so users never found what they had made.
 */
@Composable
internal fun RenderBar(
    export: ExportUiState,
    scopeLabel: String,
    subject: String?,
    renderLabel: String,
    canRender: Boolean,
    onRender: () -> Unit,
    onCancel: () -> Unit,
    onClearResult: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .fillMaxWidth()
            .crystalPanel(0.55f, MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.colorScheme.primary)
            .navigationBarsPadding()
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        when (val phase = export.phase) {
            is ExportPhase.Running, ExportPhase.Loading -> RenderProgress(phase, onCancel)
            is ExportPhase.Done -> RenderResult(export, phase, onClearResult)
            is ExportPhase.Failed ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        stringResource(R.string.export_failed, phase.message),
                        Modifier.weight(1f).semantics { liveRegion = LiveRegionMode.Polite },
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                    TextButton(onClick = onClearResult) { Text(stringResource(R.string.action_close)) }
                }
            ExportPhase.Idle -> Unit
        }
        if (!export.phase.isBusy) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Column(Modifier.weight(1f)) {
                    Text(scopeLabel, style = MaterialTheme.typography.labelMedium)
                    Text(
                        subject ?: stringResource(R.string.render_nothing_playing),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                CrystalButton(onClick = onRender, enabled = canRender) {
                    Text(renderLabel)
                }
            }
        }
    }
}

@Composable
private fun RenderProgress(
    phase: ExportPhase,
    onCancel: () -> Unit,
) {
    // Progress lands per frame; reading it only here keeps the options above from recomposing with it.
    val run by ExportRun.state.collectAsStateWithLifecycle()
    Text(
        listOfNotNull(
            stringResource(R.string.export_rendering_offline),
            run.secondsRemaining?.let { RenderEta.describe(LocalResources.current, it) },
        ).joinToString(" · "),
        style = MaterialTheme.typography.bodyMedium,
    )
    LinearProgressIndicator(progress = { phase.progress }, modifier = Modifier.fillMaxWidth())
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            stringResource(R.string.export_leave_hint),
            Modifier.weight(1f),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        TextButton(onClick = onCancel) { Text(stringResource(R.string.export_cancel)) }
    }
}

@Composable
private fun RenderResult(
    export: ExportUiState,
    phase: ExportPhase.Done,
    onClearResult: () -> Unit,
) {
    val context = LocalContext.current
    val resources = LocalResources.current
    val chooserTitle = stringResource(R.string.export_upload_share_to)
    Text(
        export.fileName?.let { stringResource(R.string.render_saved_named, it) }
            ?: stringResource(R.string.export_saved_folder),
        Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        style = MaterialTheme.typography.bodyMedium,
    )
    export.loudnessAdvice?.let { advice ->
        Text(
            advice.headline(resources) + " " + advice.detail(resources),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        CrystalButton(compact = true, onClick = { context.viewVideo(phase.resultUri) }) {
            Text(stringResource(R.string.render_open))
        }
        CrystalButton(compact = true, filled = false, onClick = {
            context.shareVideo(phase.resultUri, chooserTitle, title = export.fileName, subject = export.fileName)
        }) {
            Text(stringResource(R.string.render_share))
        }
        // A picked folder is the user's own and has no path we can open; the library folder does.
        if (!export.customDestination) {
            CrystalButton(compact = true, filled = false, onClick = { context.showRendersFolder() }) {
                Text(stringResource(R.string.render_show_in_folder))
            }
        }
        TextButton(onClick = onClearResult) { Text(stringResource(R.string.action_close)) }
    }
}

/**
 * Opens Movies/MuzViz in a file manager. Android has no one intent for "show this folder", so this
 * tries the system Files app's own directory view first, then any app that views a folder, then
 * the Files app at its start screen. Returns false when nothing on the device took any of them.
 */
internal fun Context.showRendersFolder(): Boolean {
    val folder = DocumentsContract.buildDocumentUri(EXTERNAL_STORAGE_DOCUMENTS, "primary:$RENDERS_RELATIVE_PATH")
    val attempts =
        listOfNotNull(
            Intent(Intent.ACTION_VIEW).setDataAndType(folder, DocumentsContract.Document.MIME_TYPE_DIR),
            Intent(Intent.ACTION_VIEW).setDataAndType(folder, "resource/folder"),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                Intent.makeMainSelectorActivity(Intent.ACTION_MAIN, Intent.CATEGORY_APP_FILES)
            } else {
                null
            },
        )
    return attempts.any { intent ->
        runCatching { startActivity(intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)) }.isSuccess
    }
}

private const val EXTERNAL_STORAGE_DOCUMENTS = "com.android.externalstorage.documents"

// Where VideoExporter puts a render it saves without a picked folder (its RELATIVE_PATH).
private const val RENDERS_RELATIVE_PATH = "Movies/MuzViz"
