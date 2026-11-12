package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStackState
import dev.geode.data.PaletteStackStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.model.ControlRange
import dev.geode.model.get
import dev.geode.model.with
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing

/** Each stack layer's amount: the scene param it reads and writes, by layer. */
internal val LAYER_AMOUNTS = List(8) { "paletteAmount$it" }

private const val SWATCH_SAMPLES = 24
private val BLENDS = listOf("SHARE", "MIX")

/**
 * Customize › Look › Palettes: the stack every shader style takes its colours from. Each layer is a
 * palette of the native library with its amount; + adds the next one. With no layer the style keeps
 * its own palette. Which palettes exist, what a layer paints and how they combine are all native.
 */
@Composable
internal fun PaletteStackCard(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    if (!sectionVisible(ParamScope.SHADER_LOOK)) return
    val context = LocalContext.current
    val store = remember { context.geodeContainer.paletteStack }
    val stack by store.state.collectAsStateWithLifecycle()
    Column(Modifier.fillMaxWidth().padding(top = GeodeSpacing.S)) {
        Text(
            stringResource(R.string.palette_stack_heading),
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.semantics { heading() },
        )
        ControlHint(stringResource(R.string.palette_stack_explainer))
        stack.layers.forEachIndexed { i, name -> StackLayerRow(store, stack, i, name, p, onChange) }
        if (stack.layers.size > 1) {
            DropdownRow(
                name = stringResource(R.string.palette_stack_blend),
                options = listOf(stringResource(R.string.palette_stack_share), stringResource(R.string.palette_stack_mix)),
                selectedIndex = BLENDS.indexOf(stack.blend),
            ) { store.setBlend(BLENDS[it]) }
        }
        if (stack.room > 0 && stack.picks.isNotEmpty()) {
            DropdownRow(
                name = stringResource(R.string.palette_stack_add),
                options = stack.picks.map { it.name },
                selectedIndex = -1,
                placeholder = "+",
            ) { store.add(stack.picks[it].index) }
            val yours = rememberSavedPalettes().items
            if (yours.isNotEmpty()) {
                DropdownRow(
                    name = stringResource(R.string.palette_stack_add_yours),
                    options = yours.map { it.name },
                    selectedIndex = -1,
                    placeholder = "+",
                ) { store.addColours(yours[it].name, coloursOf(yours[it])) }
            }
        }
    }
}

/** A saved palette's colours: its stops, else native's stops for its hue ramp. */
private fun coloursOf(palette: CustomPalette): List<Int> =
    palette.stops.ifEmpty {
        runCatching { GeodeNative.paletteStopsFromHues(palette.baseHue, palette.hueSpan, HUE_RAMP_STOPS).toList() }
            .getOrDefault(emptyList())
    }

private const val HUE_RAMP_STOPS = 5

@Composable
private fun StackLayerRow(
    store: PaletteStackStore,
    stack: PaletteStackState,
    layer: Int,
    name: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val amount = LAYER_AMOUNTS.getOrNull(layer) ?: return
    val swatch =
        remember(stack.text, layer) {
            runCatching { GeodeNative.paletteStackLayerSwatch(stack.text, layer, SWATCH_SAMPLES) }
                .getOrDefault(IntArray(0))
                .map(::packedColour)
        }
    val amountLabel = stringResource(R.string.palette_stack_amount, name)
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(name, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            if (layer > 0) {
                IconButton(onClick = { store.move(layer, -1) }) {
                    Icon(Icons.Filled.KeyboardArrowUp, contentDescription = stringResource(R.string.palette_stack_up, name))
                }
            }
            IconButton(onClick = { store.remove(layer) }) {
                Icon(Icons.Filled.Remove, contentDescription = stringResource(R.string.palette_stack_remove, name))
            }
        }
        if (swatch.isNotEmpty()) PaletteSwatch(swatch, Modifier.fillMaxWidth())
        Slider(
            value = p[amount],
            onValueChange = { onChange(p.with(amount, it)) },
            valueRange = ControlRange.of(amount),
            modifier = Modifier.fillMaxWidth().semantics { contentDescription = amountLabel },
        )
    }
}
