package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.audio.AudioFxFormat
import dev.geode.audio.AudioFxState
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassKnob
import dev.geode.ui.glass.GlassSlider
import dev.geode.ui.glass.GlassToggle

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
            GlassToggle(
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
        Text(
            stringResource(R.string.audio_dsp_visuals_notice),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
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
                    GlassButton(
                        text = name,
                        onClick = { onPreset(i) },
                        selected = fx.presetIndex == i,
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
        EqualizerBands(fx, onBand, controlsOn)
        if (fx.bassAvailable) {
            Text(
                stringResource(R.string.eq_bass_boost, fx.bassBoost / 10),
                style = MaterialTheme.typography.labelMedium,
            )
            GlassSlider(
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
            GlassSlider(
                value = fx.loudness.toFloat(),
                onValueChange = { onLoudness(it.toInt()) },
                valueRange = 0f..1000f,
                enabled = controlsOn,
            )
        }
    }
}

/**
 * The ten bands as pastel glass dials (docs/design/liquid-glass/README.md "Sliders … Charts …
 * Knobs: glass dials with an arc"): one [GlassKnob] per band, dragged vertically, with the band's
 * frequency label under it and its current level above — reads as a row of pastel EQ pots rather
 * than a single horizontal slider list.
 */
@Composable
private fun EqualizerBands(
    fx: AudioFxState,
    onBand: (Int, Int) -> Unit,
    controlsOn: Boolean,
) {
    if (fx.bands.isEmpty()) return
    LazyRow(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        itemsIndexed(fx.bands) { i, band ->
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text(AudioFxFormat.dbLabel(band.levelMb), style = MaterialTheme.typography.labelSmall)
                GlassKnob(
                    value = band.levelMb.toFloat(),
                    onValueChange = { onBand(i, it.toInt()) },
                    valueRange = band.minMb.toFloat()..band.maxMb.toFloat(),
                    enabled = controlsOn,
                    knobSize = 44.dp,
                )
                Text(
                    band.label,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
