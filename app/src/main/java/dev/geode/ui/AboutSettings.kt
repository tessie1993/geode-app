package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import dev.geode.BuildConfig
import dev.geode.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
internal fun AboutSettingsTab() {
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.about_group)) { AboutSection() } }
    }
}

@Composable
fun AboutSection() {
    var showLicenses by rememberSaveable { mutableStateOf(false) }
    var showPrivacy by rememberSaveable { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(stringResource(R.string.app_name), style = MaterialTheme.typography.titleSmall)
        Text(
            stringResource(R.string.about_version, BuildConfig.VERSION_NAME, BuildConfig.VERSION_CODE),
            style = MaterialTheme.typography.bodySmall,
        )
        Text(
            stringResource(R.string.about_blurb),
            style = MaterialTheme.typography.bodySmall,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { showLicenses = true }) { Text(stringResource(R.string.about_licenses)) }
            OutlinedButton(onClick = { showPrivacy = true }) { Text(stringResource(R.string.about_privacy_policy)) }
        }
    }
    if (showLicenses) {
        LicensesDialog { showLicenses = false }
    }
    if (showPrivacy) {
        PrivacyPolicyDialog { showPrivacy = false }
    }
}

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
