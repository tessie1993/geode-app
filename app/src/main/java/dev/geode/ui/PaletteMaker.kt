package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStore
import dev.geode.render.scene.SceneParams

@Stable
internal class SavedPalettes(
    private val store: PaletteStore,
) {
    var items: List<CustomPalette> by mutableStateOf(store.list())
        private set

    fun save(palette: CustomPalette): CustomPalette {
        val stored = store.save(palette)
        items = store.list()
        return stored
    }

    fun delete(id: String) {
        store.delete(id)
        items = store.list()
    }
}

@Composable
internal fun rememberSavedPalettes(): SavedPalettes {
    val context = LocalContext.current
    return remember(context) { SavedPalettes(PaletteStore(context)) }
}

internal fun paletteChipIndex(
    p: SceneParams,
    saved: List<CustomPalette>,
    second: Boolean,
): Int {
    val custom = if (second) p.usesCustomPalette2 else p.usesCustomPalette
    if (!custom) return if (second) p.palette2 else p.palette
    val id = if (second) p.customPalette2Id else p.customPaletteId
    val index = saved.indexOfFirst { it.id == id }
    return if (index < 0) -1 else SceneParams.PALETTES.size + index
}

internal fun paletteChipSelected(
    p: SceneParams,
    saved: List<CustomPalette>,
    index: Int,
    second: Boolean,
): SceneParams {
    val builtInCount = SceneParams.PALETTES.size
    return if (index < builtInCount) {
        PaletteStore.clear(if (second) p.copy(palette2 = index) else p.copy(palette = index), second)
    } else {
        PaletteStore.applyPalette(p, saved[index - builtInCount], second)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PaletteSlotSelector(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
    second: Boolean = false,
) {
    val saved = palettes.items
    val labels = SceneParams.PALETTES.map { it.first } + saved.map { it.name }
    val selected = paletteChipIndex(p, saved, second)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        labels.forEachIndexed { index, label ->
            FilterChip(
                selected = index == selected,
                onClick = { onChange(paletteChipSelected(p, saved, index, second)) },
                label = { Text(label, style = MaterialTheme.typography.labelSmall) },
            )
        }
    }
    GradientPreview(
        baseHue = if (second) p.palette2Base else p.paletteBase,
        hueSpan = if (second) p.palette2Range else p.paletteRange,
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
internal fun GradientPreview(
    baseHue: Float,
    hueSpan: Float,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .height(18.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(Brush.horizontalGradient(gradientColors(baseHue, hueSpan))),
    )
}

@Composable
internal fun PaletteMakerCard(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    var baseHue by remember { mutableFloatStateOf(p.paletteBase) }
    var hueSpan by remember { mutableFloatStateOf(p.paletteRange) }
    var name by rememberSaveable { mutableStateOf("") }
    Column {
        Text(
            "Build a gradient: the base hue is where it starts, the span is " +
                "how far around the colour wheel it sweeps (0 = one flat " +
                "colour). Apply auditions it on the scene; Save adds it to the " +
                "palette row above and to every preset you save afterwards.",
            style = MaterialTheme.typography.labelSmall,
        )
        GradientPreview(baseHue, hueSpan, modifier = Modifier.fillMaxWidth())
        Text(
            stringResource(R.string.palette_base_hue, "%.2f".format(baseHue)),
            style = MaterialTheme.typography.labelSmall,
        )
        CrystalSlider(
            value = baseHue,
            onValueChange = { baseHue = it },
            valueRange = 0f..1f,
            modifier = Modifier.fillMaxWidth(),
        )
        Text(
            stringResource(R.string.palette_hue_span, "%.2f".format(hueSpan)),
            style = MaterialTheme.typography.labelSmall,
        )
        CrystalSlider(
            value = hueSpan,
            onValueChange = { hueSpan = it },
            valueRange = 0f..1f,
            modifier = Modifier.fillMaxWidth(),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { onChange(PaletteStore.applyGradient(p, baseHue, hueSpan)) }) {
                Text(stringResource(R.string.palette_apply_gradient))
            }
            OutlinedButton(onClick = {
                baseHue = p.paletteBase
                hueSpan = p.paletteRange
            }) {
                Text(stringResource(R.string.palette_from_current))
            }
        }
        OutlinedTextField(
            value = name,
            onValueChange = { name = it },
            singleLine = true,
            label = { Text(stringResource(R.string.palette_name_label)) },
            modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
        )
        OutlinedButton(
            onClick = {
                val stored = palettes.save(PaletteStore.create(name, baseHue, hueSpan))
                name = ""
                onChange(PaletteStore.applyPalette(p, stored))
            },
            enabled = name.isNotBlank(),
        ) {
            Text(stringResource(R.string.palette_save))
        }
        Text(stringResource(R.string.palette_saved_heading), style = MaterialTheme.typography.labelSmall)
        if (palettes.items.isEmpty()) {
            Text(stringResource(R.string.palette_none_yet), style = MaterialTheme.typography.labelSmall)
        }
        palettes.items.forEach { palette ->
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(palette.name, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
                GradientPreview(palette.baseHue, palette.hueSpan, modifier = Modifier.width(64.dp))
                TextButton(onClick = {
                    baseHue = palette.baseHue
                    hueSpan = palette.hueSpan
                    name = palette.name
                }) {
                    Text(stringResource(R.string.palette_edit), style = MaterialTheme.typography.labelSmall)
                }
                TextButton(onClick = {
                    palettes.delete(palette.id)
                    onChange(PaletteStore.forgetDeleted(p, palette.id))
                }) {
                    Text(stringResource(R.string.palette_delete), style = MaterialTheme.typography.labelSmall)
                }
            }
        }
    }
}

private fun gradientColors(
    baseHue: Float,
    hueSpan: Float,
): List<Color> =
    (0 until PaletteStore.PREVIEW_STOPS).map { i ->
        val (r, g, b) = PaletteStore.hueRgb(PaletteStore.sampleHue(baseHue, hueSpan, i))
        Color(red = r, green = g, blue = b)
    }
