package dev.geode.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.text.format.Formatter
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.BuildConfig
import dev.geode.R
import dev.geode.data.StorageBucket
import dev.geode.data.StorageClearResult
import dev.geode.data.StorageUsage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Settings › Backup & about: the backup card, then the app's own rows: version, privacy policy,
 * licences, the per-app language (Android 13+), storage, and Reset all settings.
 */
@Composable
internal fun BackupAboutPage() {
    val viewModel: SettingsViewModel = geodeViewModel()
    SettingsTabColumn {
        item { BackupGroup() }
        item { SettingsGroup(stringResource(R.string.about_group)) { AboutSection() } }
        item { SettingsGroup(stringResource(R.string.settings_storage_section)) { StorageAndResetSection(viewModel) } }
    }
}

@Composable
private fun AboutSection() {
    val context = LocalContext.current
    var showLicenses by rememberSaveable { mutableStateOf(false) }
    var showPrivacy by rememberSaveable { mutableStateOf(false) }
    SettingsRow(
        title = stringResource(R.string.app_name),
        summary = stringResource(R.string.about_version, BuildConfig.VERSION_NAME, BuildConfig.VERSION_CODE),
    )
    SettingsHint(stringResource(R.string.about_blurb))
    SettingsRow(title = stringResource(R.string.about_privacy_policy), onClick = { showPrivacy = true })
    SettingsRow(title = stringResource(R.string.about_licenses), onClick = { showLicenses = true })
    // Android 13 added a per-app language page; below it the system language is the only one.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        SettingsRow(
            title = stringResource(R.string.settings_language),
            summary = stringResource(R.string.settings_language_summary),
            onClick = { context.openAppLanguageSettings() },
        )
    }
    if (showLicenses) {
        LicensesDialog { showLicenses = false }
    }
    if (showPrivacy) {
        PrivacyPolicyDialog { showPrivacy = false }
    }
}

/** Android's "App language" page for this app (API 33+), else the app's own info page. */
private fun Context.openAppLanguageSettings() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        val page =
            Intent(Settings.ACTION_APP_LOCALE_SETTINGS, Uri.fromParts("package", packageName, null))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (runCatching { startActivity(page) }.isSuccess) return
    }
    openAppSettings()
}

/**
 * Storage (the caches the app keeps on disk, and emptying them) and Reset all settings. A reset
 * asks first and ends in a restart, since every store still holds its old copy in memory
 * ([dev.geode.data.SettingsReset]).
 */
@Composable
private fun StorageAndResetSection(viewModel: SettingsViewModel) {
    val context = LocalContext.current
    val usage by viewModel.storage.collectAsStateWithLifecycle()
    val resetDone by viewModel.resetDone.collectAsStateWithLifecycle()
    var storageOpen by rememberSaveable { mutableStateOf(false) }
    var confirmReset by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(Unit) { viewModel.refreshStorage() }
    SettingsRow(
        title = stringResource(R.string.settings_storage),
        summary =
            usage?.let { stringResource(R.string.settings_storage_summary, Formatter.formatShortFileSize(context, it.clearableBytes())) }
                ?: stringResource(R.string.settings_storage_measuring),
        onClick = { storageOpen = true },
    )
    SettingsRow(
        title = stringResource(R.string.settings_reset),
        summary = stringResource(R.string.settings_reset_summary),
        onClick = { confirmReset = true },
    )
    if (storageOpen) StorageDialog(viewModel, usage) { storageOpen = false }
    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.settings_reset_confirm_title)) },
            text = { Text(stringResource(R.string.settings_reset_confirm_body)) },
            confirmButton = {
                TextButton(onClick = {
                    confirmReset = false
                    viewModel.resetAllSettings()
                }) { Text(stringResource(R.string.settings_reset_confirm)) }
            },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.action_cancel)) } },
        )
    }
    resetDone?.let { ok ->
        // Not dismissable, like the restore's: any store's next save would write the old values back.
        AlertDialog(
            onDismissRequest = {},
            title = { Text(stringResource(if (ok) R.string.settings_reset_done_title else R.string.settings_reset_failed_title)) },
            text = { Text(stringResource(R.string.settings_reset_done_body)) },
            confirmButton = { TextButton(onClick = { restartApp(context) }) { Text(stringResource(R.string.backup_restart)) } },
        )
    }
}

/** The caches Storage can empty. Settings never offers to delete the user's videos. */
private val CLEARABLE = listOf(StorageBucket.RENDER_SCRATCH, StorageBucket.CACHE)

@Composable
private fun StorageDialog(
    viewModel: SettingsViewModel,
    usage: StorageUsage?,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val report: (StorageClearResult) -> Unit = { result ->
        val message =
            if (result.skipped > 0) {
                context.getString(R.string.settings_storage_kept)
            } else {
                context.getString(R.string.settings_storage_freed, Formatter.formatShortFileSize(context, result.freedBytes))
            }
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.settings_storage)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                CLEARABLE.forEach { bucket ->
                    val bytes = usage?.get(bucket)
                    SettingsRow(
                        title =
                            stringResource(
                                if (bucket == StorageBucket.CACHE) R.string.settings_storage_cache else R.string.settings_storage_scratch,
                            ),
                        summary =
                            bytes?.let { Formatter.formatShortFileSize(context, it) }
                                ?: stringResource(R.string.settings_storage_measuring),
                        trailing = {
                            TextButton(
                                enabled = bytes != null && bytes > 0,
                                onClick = { viewModel.clearStorage(bucket, report) },
                            ) { Text(stringResource(R.string.settings_storage_clear)) }
                        },
                    )
                }
                SettingsHint(stringResource(R.string.settings_storage_cache_hint))
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) } },
    )
}

/** What the clearable caches hold together; the size the Storage row shows. */
private fun StorageUsage.clearableBytes(): Long = CLEARABLE.sumOf { this[it] }

/**
 * The privacy policy, read from the APK rather than a web page. The app has no network
 * permission, so a link out would depend on a browser and a connection the app itself
 * promises not to need; the bundled copy is always there and always the version that
 * shipped. `assets/privacy_policy.md` is a copy of `docs/privacy-policy.md`, and
 * `PrivacyPolicyAssetTest` fails the build when the two drift apart.
 */
@Composable
private fun PrivacyPolicyDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var lines by remember { mutableStateOf<List<PolicyLine>?>(null) }
    LaunchedEffect(Unit) {
        lines =
            withContext(Dispatchers.IO) {
                runCatching {
                    context.assets.open("privacy_policy.md").use { policyLines(it.readBytes().decodeToString()) }
                }.getOrElse { emptyList() }
            }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.about_privacy_policy)) },
        text = {
            Column(
                Modifier
                    .heightIn(max = 420.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                val loaded = lines
                when {
                    loaded == null -> Text(stringResource(R.string.about_licenses_loading))
                    loaded.isEmpty() -> Text(stringResource(R.string.about_privacy_unavailable))
                    else ->
                        loaded.forEach { line ->
                            when (line) {
                                is PolicyLine.Heading ->
                                    Text(
                                        line.text,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = FontWeight.SemiBold,
                                        modifier = Modifier.padding(top = 6.dp),
                                    )
                                is PolicyLine.Bullet -> Text("•  ${line.text}", style = MaterialTheme.typography.bodySmall)
                                is PolicyLine.Paragraph -> Text(line.text, style = MaterialTheme.typography.bodySmall)
                            }
                        }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) } },
    )
}

internal sealed interface PolicyLine {
    val text: String

    data class Heading(
        override val text: String,
    ) : PolicyLine

    data class Paragraph(
        override val text: String,
    ) : PolicyLine

    data class Bullet(
        override val text: String,
    ) : PolicyLine
}

/**
 * Just enough Markdown for the one document it reads: headings, paragraphs (hard-wrapped
 * lines joined back up), bullets, and table rows turned into bullets ("What: why. Where.").
 * Emphasis and code marks are dropped and a link keeps its address in brackets, since
 * there is nothing in the dialog to tap. The policy is written for this, not the other
 * way round, so anything fancier added to it should be checked in the app.
 */
internal fun policyLines(markdown: String): List<PolicyLine> {
    val out = mutableListOf<PolicyLine>()
    val paragraph = StringBuilder()
    var bullet: StringBuilder? = null

    fun flush() {
        bullet?.let { out += PolicyLine.Bullet(it.toString()) }
        bullet = null
        if (paragraph.isNotEmpty()) out += PolicyLine.Paragraph(paragraph.toString())
        paragraph.clear()
    }
    var tableHeader = true
    for (raw in markdown.lines()) {
        val line = raw.trim()
        when {
            line.isEmpty() -> {
                flush()
                tableHeader = true
            }
            line.startsWith("#") -> {
                flush()
                out += PolicyLine.Heading(plain(line.trimStart('#').trim()))
            }
            line.startsWith("|") -> {
                flush()
                val cells = line.trim('|').split('|').map { plain(it.trim()) }
                when {
                    cells.all { it.isEmpty() || it.all { c -> c == '-' || c == ':' } } -> Unit
                    tableHeader -> tableHeader = false
                    else -> out += PolicyLine.Bullet(tableRow(cells))
                }
            }
            line.startsWith("- ") -> {
                flush()
                bullet = StringBuilder(plain(line.removePrefix("- ")))
            }
            else -> {
                val target = bullet ?: paragraph
                if (target.isNotEmpty()) target.append(' ')
                target.append(plain(line))
            }
        }
    }
    flush()
    return out
}

private fun tableRow(cells: List<String>): String =
    buildString {
        append(cells.first())
        cells.drop(1).filter { it.isNotEmpty() }.forEachIndexed { i, cell ->
            append(if (i == 0) ": " else " ")
            append(cell)
            if (!cell.endsWith('.')) append('.')
        }
    }

private val LINK = Regex("""\[([^\]]+)]\(([^)]+)\)""")

private fun plain(text: String): String =
    text
        .replace(LINK) { m ->
            val label = m.groupValues[1]
            val target = m.groupValues[2].removePrefix("mailto:")
            if (label == target) label else "$label ($target)"
        }.replace("**", "")
        .replace("`", "")
        .replace(Regex("""(?<![\w*])\*(?!\s)([^*]+?)\*(?!\w)"""), "$1")

@Composable
private fun LicensesDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    val unavailable = stringResource(R.string.about_licenses_unavailable)
    var notices by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        notices =
            withContext(Dispatchers.IO) {
                runCatching {
                    context.assets.open("third_party_notices.txt").use { it.readBytes().decodeToString() }
                }.getOrElse { unavailable }
            }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.about_licenses)) },
        text = {
            Column(
                Modifier
                    .heightIn(max = 420.dp)
                    .verticalScroll(rememberScrollState()),
            ) {
                Text(
                    notices ?: stringResource(R.string.about_licenses_loading),
                    style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                )
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) } },
    )
}
