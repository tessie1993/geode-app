package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.AudioFxFormat
import dev.geode.audio.AudioFxState

@Composable
fun EqualizerSettings(viewModel: SettingsViewModel) {
    val fx by viewModel.audioFx.collectAsStateWithLifecycle()
    EqualizerCard(
        fx = fx,
        onEnabled = viewModel::setAudioFxEnabled,
        onPreset = viewModel::useAudioFxPreset,
        onBand = viewModel::setAudioFxBand,
        onBassBoost = viewModel::setAudioFxBassBoost,
        onLoudness = viewModel::setAudioFxLoudness,
    )
}

@Composable
internal fun EqualizerCard(
    fx: AudioFxState,
    onEnabled: (Boolean) -> Unit,
    onPreset: (Int) -> Unit,
    onBand: (Int, Int) -> Unit,
    onBassBoost: (Int) -> Unit,
    onLoudness: (Int) -> Unit,
) {
    val anyEffect = fx.available || fx.bassAvailable || fx.loudnessAvailable
    SettingsGroup(
        title = stringResource(R.string.eq_title),
        header = {
            Switch(
                checked = fx.enabled && anyEffect,
                onCheckedChange = onEnabled,
                enabled = anyEffect,
            )
        },
    ) {
        if (!fx.attached) {
            Text(
                stringResource(R.string.eq_no_session),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            return@SettingsGroup
        }
        if (!anyEffect) {
            Text(
                stringResource(R.string.eq_unsupported),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            return@SettingsGroup
        }
        val controlsOn = fx.enabled
        if (!fx.available) {
            Text(
                stringResource(R.string.eq_partial),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (fx.presets.isNotEmpty()) {
            Text(stringResource(R.string.eq_preset), style = MaterialTheme.typography.labelMedium)
            LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                itemsIndexed(fx.presets) { i, name ->
                    FilterChip(
                        selected = fx.presetIndex == i,
                        onClick = { onPreset(i) },
                        label = { Text(name) },
                        enabled = controlsOn,
                    )
                }
            }
            if (fx.presetIndex < 0) {
                Text(
                    stringResource(R.string.eq_custom),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        fx.bands.forEachIndexed { i, band ->
            Text(
                stringResource(R.string.eq_band, band.label, AudioFxFormat.dbLabel(band.levelMb)),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = band.levelMb.toFloat(),
                onValueChange = { onBand(i, it.toInt()) },
                valueRange = band.minMb.toFloat()..band.maxMb.toFloat(),
                enabled = controlsOn,
            )
        }
        if (fx.bassAvailable) {
            Text(
                stringResource(R.string.eq_bass_boost, fx.bassBoost / 10),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = fx.bassBoost.toFloat(),
                onValueChange = { onBassBoost(it.toInt()) },
                valueRange = 0f..1000f,
                enabled = controlsOn,
            )
        }
        if (fx.loudnessAvailable) {
            Text(
                stringResource(R.string.eq_loudness, AudioFxFormat.dbLabel(fx.loudness)),
                style = MaterialTheme.typography.labelMedium,
            )
            CrystalSlider(
                value = fx.loudness.toFloat(),
                onValueChange = { onLoudness(it.toInt()) },
                valueRange = 0f..1000f,
                enabled = controlsOn,
            )
        }
    }
}
