package dev.geode.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import dev.geode.R
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDefaults
import dev.geode.data.ExportDestination
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.SaveTarget
import dev.geode.data.exportQualityLabel
import dev.geode.export.ExportQuality
import dev.geode.export.ExportRatio
import dev.geode.pro.FreeTier
import dev.geode.pro.ProFeature
import dev.geode.ui.theme.GeodeSpacing
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** The two answers to "Save videos to": MediaStore's Movies/MuzViz, or a document tree. */
private enum class SaveChoice { LIBRARY, FOLDER }

/** What Settings › Export & save holds once its one read on IO is back. */
private data class ExportSettingsState(
    val saveTo: SaveTarget,
    val destinations: Map<ExportDestination, ExportDefaults>,
)

/**
 * Settings › Export & save: where videos are saved, and each destination's export defaults
 * ([ExportPrefsStore.loadFor]), which a new Studio project copies. The render itself and its
 * working options stay in Studio › Render, which the first row opens.
 */
@Composable
internal fun ExportSettingsPage(onOpenStudioRender: () -> Unit) {
    val ctx = LocalContext.current
    val store = remember { ExportPrefsStore(GeodePrefsFiles(ctx).general) }
    var state by remember { mutableStateOf<ExportSettingsState?>(null) }
    LaunchedEffect(store) {
        state =
            withContext(Dispatchers.IO) {
                ExportSettingsState(store.load().saveTo, ExportDestination.entries.associateWith(store::loadFor))
            }
    }
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            val granted =
                runCatching {
                    ctx.contentResolver.takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
                    )
                }.isSuccess
            if (granted) {
                val target = SaveTarget.Folder(uri.toString())
                store.save(store.load().copy(saveTo = target))
                state = state?.copy(saveTo = target)
            }
        }
    var editing by rememberSaveable { mutableStateOf<String?>(null) }
    SettingsTabColumn {
        item {
            SettingsGroup(stringResource(R.string.settings_export_section_render)) {
                SettingsRow(
                    title = stringResource(R.string.settings_export_open_studio),
                    summary = stringResource(R.string.settings_export_moved),
                    onClick = onOpenStudioRender,
                )
            }
        }
        val loaded = state ?: return@SettingsTabColumn
        item {
            SettingsGroup(stringResource(R.string.settings_export_section_save)) {
                val saveTo = loaded.saveTo
                SettingsChoiceRow(
                    title = stringResource(R.string.settings_export_save_to),
                    options = SaveChoice.entries,
                    selected = if (saveTo is SaveTarget.Folder) SaveChoice.FOLDER else SaveChoice.LIBRARY,
                    label = { choice ->
                        when {
                            choice == SaveChoice.LIBRARY -> stringResource(R.string.settings_export_save_library)
                            saveTo is SaveTarget.Folder -> stringResource(R.string.settings_export_save_folder_named, folderName(saveTo))
                            else -> stringResource(R.string.settings_export_save_folder)
                        }
                    },
                    onSelect = { choice ->
                        if (choice == SaveChoice.FOLDER) {
                            folderPicker.launchPicker(ctx, null)
                        } else {
                            store.save(store.load().copy(saveTo = SaveTarget.VideosLibrary))
                            state = loaded.copy(saveTo = SaveTarget.VideosLibrary)
                        }
                    },
                )
            }
        }
        item {
            SettingsGroup(stringResource(R.string.settings_export_section_destinations)) {
                SettingsHint(stringResource(R.string.settings_export_destinations_hint))
                ExportDestination.entries.forEach { destination ->
                    val defaults = loaded.destinations[destination] ?: destination.factoryDefaults()
                    SettingsRow(
                        title = destinationName(destination),
                        summary = defaultsSummary(defaults),
                        onClick = { editing = destination.name },
                    )
                }
            }
        }
    }
    val current = state
    val destination = ExportDestination.entries.firstOrNull { it.name == editing }
    if (current != null && destination != null) {
        DestinationDialog(
            destination = destination,
            initial = current.destinations[destination] ?: destination.factoryDefaults(),
            onSave = { next ->
                store.saveFor(destination, next)
                state = current.copy(destinations = current.destinations + (destination to next))
                editing = null
            },
            onReset = {
                store.resetFor(destination)
                state = current.copy(destinations = current.destinations + (destination to destination.factoryDefaults()))
                editing = null
            },
            onDismiss = { editing = null },
        )
    }
}

/** Quality, frame rate, shape and looping for one destination. 4K and above 30 fps are Pro. */
@Composable
private fun DestinationDialog(
    destination: ExportDestination,
    initial: ExportDefaults,
    onSave: (ExportDefaults) -> Unit,
    onReset: () -> Unit,
    onDismiss: () -> Unit,
) {
    val pro = rememberProAccess()
    var draft by remember(destination) { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(destinationName(destination)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.M)) {
                ChipLine(
                    title = stringResource(R.string.settings_export_quality),
                    options = ExportQuality.entries,
                    selected = draft.quality,
                    label = { exportQualityLabel(it) },
                    isPro = { it == ExportQuality.UHD4K },
                    onSelect = { q ->
                        if (q == ExportQuality.UHD4K) {
                            pro.runOrPaywall(ProFeature.EXPORT_4K) { draft = draft.copy(quality = q) }
                        } else {
                            draft = draft.copy(quality = q)
                        }
                    },
                )
                ChipLine(
                    title = stringResource(R.string.settings_export_fps),
                    options = EXPORT_FPS_OPTIONS,
                    selected = draft.fps,
                    label = { stringResource(R.string.settings_export_fps_value, it) },
                    isPro = { it > FreeTier.FREE_MAX_FPS },
                    onSelect = { fps ->
                        if (fps > FreeTier.FREE_MAX_FPS) {
                            pro.runOrPaywall(ProFeature.EXPORT_60FPS) { draft = draft.copy(fps = fps) }
                        } else {
                            draft = draft.copy(fps = fps)
                        }
                    },
                )
                ChipLine(
                    title = stringResource(R.string.settings_export_ratio),
                    options = ExportRatio.entries,
                    selected = draft.ratio,
                    label = { it.label },
                    onSelect = { draft = draft.copy(ratio = it) },
                )
                SettingsSwitchRow(
                    title = stringResource(R.string.settings_export_loop),
                    checked = draft.loopSafe,
                    onCheckedChange = { draft = draft.copy(loopSafe = it) },
                )
            }
        },
        confirmButton = { TextButton(onClick = { onSave(draft) }) { Text(stringResource(R.string.action_save)) } },
        dismissButton = {
            Row {
                TextButton(onClick = onReset) { Text(stringResource(R.string.settings_export_reset)) }
                TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) }
            }
        },
    )
}

@Composable
private fun <T> ChipLine(
    title: String,
    options: List<T>,
    selected: T,
    label: @Composable (T) -> String,
    onSelect: (T) -> Unit,
    isPro: (T) -> Boolean = { false },
) {
    Column(verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS)) {
        Text(title, style = MaterialTheme.typography.labelLarge)
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            options.forEach { option ->
                FilterChip(
                    selected = option == selected,
                    onClick = { onSelect(option) },
                    label = {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
                        ) {
                            Text(label(option))
                            if (isPro(option)) ProCrown()
                        }
                    },
                )
            }
        }
    }
}

@Composable
private fun destinationName(destination: ExportDestination): String =
    stringResource(destination.preset?.nameRes ?: R.string.settings_export_custom)

@Composable
private fun defaultsSummary(d: ExportDefaults): String {
    val parts =
        listOfNotNull(
            exportQualityLabel(d.quality),
            stringResource(R.string.settings_export_fps_value, d.fps),
            d.ratio.label,
            stringResource(R.string.settings_export_summary_loop).takeIf { d.loopSafe },
        )
    return parts.joinToString(" · ")
}

/** The last segment of a document tree, e.g. "primary:Movies/Clips" reads as "Movies/Clips". */
private fun folderName(target: SaveTarget.Folder): String {
    val doc = runCatching { Uri.parse(target.treeUri).lastPathSegment }.getOrNull() ?: return target.treeUri
    return doc.substringAfter(':').ifBlank { doc }
}
