package dev.geode.ui.studio

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.AutoCut
import dev.geode.editor.AutoCutMiss
import dev.geode.editor.AutoCutResult
import dev.geode.editor.AutoCutSettings
import dev.geode.editor.TransientEnvelope
import dev.geode.editor.TransientHit
import dev.geode.editor.TransientSource
import dev.geode.ui.CrystalButton
import dev.geode.ui.CrystalSlider

/** Runs [AutoCut] over the analysed track and hands the accepted hits back as markers or clips. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AutoCutSheet(
    envelopeFor: (TransientSource) -> TransientEnvelope?,
    onMarkers: (List<TransientHit>) -> Unit,
    onClips: (List<TransientHit>, untilMs: Long) -> Unit,
    onDismiss: () -> Unit,
) {
    var settings by remember { mutableStateOf(AutoCutSettings()) }
    var result by remember { mutableStateOf<AutoCutResult?>(null) }
    var envelope by remember { mutableStateOf<TransientEnvelope?>(null) }
    var missingAnalysis by remember { mutableStateOf(false) }

    fun detect() {
        val found = envelopeFor(settings.source)
        envelope = found
        missingAnalysis = found == null
        result = found?.let { AutoCut.detect(it, settings) }
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.autocut_title), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.autocut_source), style = MaterialTheme.typography.labelMedium)
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                TransientSource.entries.forEach { source ->
                    FilterChip(
                        selected = settings.source == source,
                        onClick = { settings = settings.copy(source = source) },
                        label = { Text(stringResource(sourceLabel(source)), style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
            Text(
                stringResource(R.string.autocut_sensitivity, (settings.sensitivity * 100).toInt()),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(value = settings.sensitivity, onValueChange = { settings = settings.copy(sensitivity = it) })
            Text(stringResource(R.string.autocut_spacing, settings.minSpacingMs), style = MaterialTheme.typography.labelMedium)
            CrystalSlider(
                value = settings.minSpacingMs.toFloat(),
                onValueChange = { settings = settings.copy(minSpacingMs = (it / 10f).toLong() * 10L) },
                valueRange = 60f..2000f,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(onClick = ::detect) { Text(stringResource(R.string.autocut_detect)) }
            }
            when (val r = result) {
                null -> if (missingAnalysis) ResultText(stringResource(R.string.autocut_no_analysis), error = true)
                is AutoCutResult.NoCuts -> ResultText(stringResource(missLabel(r.reason)), error = true)
                is AutoCutResult.Suggested -> {
                    ResultText(pluralStringResource(R.plurals.autocut_found, r.hits.size, r.hits.size), error = false)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        CrystalButton(onClick = { onMarkers(r.hits) }) { Text(stringResource(R.string.autocut_as_markers)) }
                        CrystalButton(filled = false, onClick = { onClips(r.hits, envelope?.durationMs ?: 0L) }) {
                            Text(stringResource(R.string.autocut_as_clips))
                        }
                    }
                }
            }
            Text(
                stringResource(R.string.autocut_explainer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun ResultText(
    text: String,
    error: Boolean,
) {
    Text(
        text,
        style = MaterialTheme.typography.bodyMedium,
        color = if (error) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
    )
}

private fun sourceLabel(source: TransientSource): Int =
    when (source) {
        TransientSource.TRANSIENT -> R.string.autocut_source_transient
        TransientSource.SPECTRAL_FLUX -> R.string.autocut_source_flux
        TransientSource.ONSET -> R.string.autocut_source_onset
        TransientSource.KICK -> R.string.autocut_source_kick
        TransientSource.SNARE -> R.string.autocut_source_snare
        TransientSource.HAT -> R.string.autocut_source_hat
    }

private fun missLabel(miss: AutoCutMiss): Int =
    when (miss) {
        AutoCutMiss.EmptyEnvelope -> R.string.autocut_miss_empty
        AutoCutMiss.NoTransients -> R.string.autocut_miss_none
        is AutoCutMiss.WindowTooShort -> R.string.autocut_miss_window
    }
