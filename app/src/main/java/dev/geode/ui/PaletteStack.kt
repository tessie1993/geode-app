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
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.data.PaletteStackStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing

/**
 * Customize › Look › Palettes: the stack every shader style takes its colours from, and Look's one palette
 * list on those styles. The card is native's (core/viz/PalettePanel `stackCard`): its words, each layer's
 * row, swatch and amount param, and whether Blend and Add show; this draws the rows and sends each edit to
 * the store, which hands it to native.
 */
@Composable
internal fun PaletteStackCard(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val context = LocalContext.current
    val store = remember { context.geodeContainer.paletteStack }
    val stack by store.state.collectAsStateWithLifecycle()
    val yours = rememberSavedPalettes().items
    val rows = remember(stack.text, p, yours.size) { nativeRows(GeodeNative.paletteStackCard(stack.text, p.frame(), yours.size)) }
    val blends = rows.filter { it[0] == "blendoption" }
    val picks = rows.filter { it[0] == "pick" }
    Column(Modifier.fillMaxWidth().padding(top = GeodeSpacing.S)) {
        rows.forEach { f ->
            when (f[0]) {
                "card" -> {
                    Text(f[2], style = MaterialTheme.typography.titleSmall, modifier = Modifier.semantics { heading() })
                    ControlHint(f[3])
                }
                "layer" -> StackLayerRow(store, f, p, onChange)
                "blend" ->
                    DropdownRow(name = f[1], options = blends.map { it[2] }, selectedIndex = f[2].toInt()) { store.setBlend(blends[it][1]) }
                "add" ->
                    DropdownRow(name = f[1], options = picks.map { it[2] }, selectedIndex = -1, placeholder = f[2]) {
                        store.add(picks[it][1].toInt())
                    }
                "yours" ->
                    DropdownRow(name = f[1], options = yours.map { it.name }, selectedIndex = -1, placeholder = f[2]) {
                        val palette = yours[it]
                        val colours = GeodeNative.paletteSavedColours(palette.stops.toIntArray(), palette.baseHue, palette.hueSpan)
                        store.addColours(palette.name, colours.toList())
                    }
                else -> Unit
            }
        }
    }
}

/** One layer's row as native lists it (core/viz/PalettePanel.hpp names the fields). */
@Composable
private fun StackLayerRow(
    store: PaletteStackStore,
    f: List<String>,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val layer = f[1].toInt()
    val swatch = remember(f[11]) { swatchColours(f[11]) }
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(f[2], style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            if (f[7] == "1") {
                IconButton(onClick = { store.move(layer, -1) }) {
                    Icon(Icons.Filled.KeyboardArrowUp, contentDescription = f[8])
                }
            }
            IconButton(onClick = { store.remove(layer) }) {
                Icon(Icons.Filled.Remove, contentDescription = f[9])
            }
        }
        PaletteSwatch(swatch, Modifier.fillMaxWidth())
        Slider(
            value = f[4].toFloat(),
            onValueChange = { onChange(p.with(f[3], it)) },
            valueRange = f[5].toFloat()..f[6].toFloat(),
            modifier = Modifier.fillMaxWidth().semantics { contentDescription = f[10] },
        )
    }
}
