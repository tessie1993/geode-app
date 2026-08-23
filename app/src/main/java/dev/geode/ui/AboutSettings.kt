package dev.geode.ui

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
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
import androidx.compose.ui.unit.dp
import dev.geode.BuildConfig
import dev.geode.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

const val PRIVACY_POLICY_URL = "https://tessie1993.github.io/music-visualizer-2/privacy-policy.html"

@Composable
internal fun AboutSettingsTab() {
    SettingsTabColumn {
        item { SettingsGroup(stringResource(R.string.about_group)) { AboutSection() } }
    }
}

@Composable
fun AboutSection() {
    var showLicenses by rememberSaveable { mutableStateOf(false) }
    val context = LocalContext.current
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
            OutlinedButton(onClick = {
                runCatching {
                    context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(PRIVACY_POLICY_URL)))
                }
            }) { Text(stringResource(R.string.about_privacy_policy)) }
        }
    }
    if (showLicenses) {
        LicensesDialog { showLicenses = false }
    }
}

@Composable
private fun LicensesDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var notices by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        notices =
            withContext(Dispatchers.IO) {
                runCatching {
                    context.assets.open("third_party_notices.txt").use { it.readBytes().decodeToString() }
                }.getOrElse { context.getString(R.string.about_licenses_unavailable) }
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
