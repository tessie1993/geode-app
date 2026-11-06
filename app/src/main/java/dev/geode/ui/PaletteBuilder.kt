package dev.geode.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Colorize
import androidx.compose.material3.AssistChip
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import dev.geode.R
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.delay

private fun hsvToRgb(
    hue: Float,
    saturation: Float,
    value: Float,
): Int {
    val hsv = floatArrayOf(hue.coerceIn(0f, 360f), saturation.coerceIn(0f, 1f), value.coerceIn(0f, 1f))
    return android.graphics.Color.HSVToColor(hsv) and 0xFFFFFF
}

private fun rgbToHsv(rgb: Int): FloatArray = FloatArray(3).also { android.graphics.Color.colorToHSV(0xFF000000.toInt() or rgb, it) }

private fun hexOf(rgb: Int): String = "%06X".format(rgb and 0xFFFFFF)

/**
 * Makes a palette: pick a colour on the wheel (or type it, or take it from the live visual), tap + to put it in
 * the row, and the row is the gradient, painted across the style in that order. A swatch in the row can be
 * tapped to edit it; a long press moves or removes it. The matching-colour chips add a complementary, triad,
 * analogous or split-complementary set in one tap (the colour maths is native, core/viz/Palette). "Try it"
 * auditions the row on the visual; Save keeps it under a name, in the list below, for Customize and for presets.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PaletteBuilderCard(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    var hue by remember { mutableFloatStateOf(210f) }
    var saturation by remember { mutableFloatStateOf(0.7f) }
    var value by remember { mutableFloatStateOf(1f) }
    val row = remember { mutableStateListOf<Int>() }
    var selected by remember { mutableIntStateOf(-1) }
    var name by rememberSaveable { mutableStateOf("") }
    var picking by remember { mutableStateOf(false) }
    val rgb = hsvToRgb(hue, saturation, value)

    fun setColor(
        h: Float,
        s: Float,
        v: Float,
    ) {
        hue = h
        saturation = s
        value = v
        if (selected in row.indices) row[selected] = hsvToRgb(h, s, v)
    }

    fun loadColor(color: Int) {
        val hsv = rgbToHsv(color)
        hue = hsv[0]
        saturation = hsv[1]
        value = hsv[2]
        if (selected in row.indices) row[selected] = color
    }

    fun addToRow(color: Int) {
        if (row.size < PaletteStore.MAX_STOPS) {
            row.add(color)
            selected = row.lastIndex
        }
    }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(stringResource(R.string.palette_explainer), style = MaterialTheme.typography.labelSmall)
        ColorWheel(
            hue = hue,
            saturation = saturation,
            value = value,
            onChange = ::setColor,
            modifier = Modifier.align(Alignment.CenterHorizontally),
        )
        ColourFields(rgb = rgb, onRgb = ::loadColor)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).background(swatchColor(rgb), RoundedCornerShape(8.dp)))
            FilledTonalIconButton(
                onClick = {
                    selected = -1
                    addToRow(rgb)
                },
                enabled = row.size < PaletteStore.MAX_STOPS,
            ) {
                Icon(Icons.Filled.Add, contentDescription = stringResource(R.string.palette_add_colour))
            }
            OutlinedButton(onClick = { picking = true }) {
                Icon(Icons.Filled.Colorize, contentDescription = null, modifier = Modifier.size(18.dp))
                Text(stringResource(R.string.palette_eyedropper), modifier = Modifier.padding(start = 6.dp))
            }
        }
        Text(stringResource(R.string.palette_harmony_heading), style = MaterialTheme.typography.labelSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            HARMONY_LABELS.forEachIndexed { kind, label ->
                AssistChip(
                    onClick = {
                        if (row.isEmpty()) addToRow(rgb)
                        GeodeNative.paletteHarmony(rgb, kind).forEach { color -> if (row.size < PaletteStore.MAX_STOPS) row.add(color) }
                    },
                    label = { Text(stringResource(label), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        Text(stringResource(R.string.palette_row_heading, PaletteStore.MAX_STOPS), style = MaterialTheme.typography.labelSmall)
        if (row.isEmpty()) {
            Text(stringResource(R.string.palette_row_empty), style = MaterialTheme.typography.bodySmall)
        } else {
            GradientPreview(row.toList(), modifier = Modifier.fillMaxWidth())
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                row.forEachIndexed { index, color ->
                    Swatch(
                        color = color,
                        index = index,
                        count = row.size,
                        selected = index == selected,
                        onSelect = {
                            selected = index
                            loadColor(color)
                        },
                        onMove = { by ->
                            val to = (index + by).coerceIn(0, row.lastIndex)
                            row.add(to, row.removeAt(index))
                            if (selected == index) selected = to
                        },
                        onRemove = {
                            row.removeAt(index)
                            selected = -1
                        },
                    )
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(
                onClick = { onChange(PaletteStore.applyPalette(p, palettes.audition(row.toList()))) },
                enabled = row.isNotEmpty(),
            ) { Text(stringResource(R.string.palette_try)) }
            TextButton(
                onClick = {
                    row.clear()
                    selected = -1
                },
                enabled = row.isNotEmpty(),
            ) { Text(stringResource(R.string.palette_clear_row)) }
        }
        OutlinedTextField(
            value = name,
            onValueChange = { name = it },
            singleLine = true,
            label = { Text(stringResource(R.string.palette_name_label)) },
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedButton(
            onClick = {
                val stored = palettes.save(PaletteStore.create(name, row.toList()))
                name = ""
                onChange(PaletteStore.applyPalette(p, stored))
            },
            enabled = name.isNotBlank() && row.isNotEmpty(),
        ) { Text(stringResource(R.string.palette_save)) }
        SavedPaletteList(
            p = p,
            onChange = onChange,
            palettes = palettes,
            onEdit = { palette ->
                row.clear()
                row.addAll(palette.stops)
                selected = -1
                name = if (palette.builtIn) "" else palette.name
                palette.stops.firstOrNull()?.let { color ->
                    val hsv = rgbToHsv(color)
                    hue = hsv[0]
                    saturation = hsv[1]
                    value = hsv[2]
                }
            },
        )
        PaletteSequenceEditor(palettes)
    }
    if (picking) {
        EyedropperOverlay(
            onPicked = { color ->
                picking = false
                selected = -1
                loadColor(color)
                addToRow(color)
            },
            onCancel = { picking = false },
        )
    }
}

private val HARMONY_LABELS =
    listOf(
        R.string.palette_harmony_complementary,
        R.string.palette_harmony_triad,
        R.string.palette_harmony_analogous,
        R.string.palette_harmony_split,
    )

/** A colour in the row: tap to edit it, long press for move left, move right and remove. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun Swatch(
    color: Int,
    index: Int,
    count: Int,
    selected: Boolean,
    onSelect: () -> Unit,
    onMove: (Int) -> Unit,
    onRemove: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    val description = stringResource(R.string.palette_swatch_a11y, index + 1, count, hexOf(color))
    val outline = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline
    Box {
        Box(
            Modifier
                .size(44.dp)
                .background(swatchColor(color), RoundedCornerShape(10.dp))
                .border(if (selected) 3.dp else 1.dp, outline, RoundedCornerShape(10.dp))
                .semantics { contentDescription = description }
                .combinedClickable(
                    onClick = onSelect,
                    onLongClick = { menu = true },
                    onLongClickLabel = stringResource(R.string.palette_swatch_menu),
                ),
        )
        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.palette_move_left)) },
                enabled = index > 0,
                onClick = {
                    menu = false
                    onMove(-1)
                },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.palette_move_right)) },
                enabled = index < count - 1,
                onClick = {
                    menu = false
                    onMove(1)
                },
            )
            DropdownMenuItem(
                text = { Text(stringResource(R.string.palette_remove)) },
                onClick = {
                    menu = false
                    onRemove()
                },
            )
        }
    }
}

/** Hex and R, G, B entry for the current colour. */
@Composable
private fun ColourFields(
    rgb: Int,
    onRgb: (Int) -> Unit,
) {
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        EntryField(
            shown = hexOf(rgb),
            label = stringResource(R.string.palette_hex_label),
            modifier = Modifier.weight(1.7f),
            keyboard = KeyboardType.Ascii,
            parse = { t -> t.removePrefix("#").takeIf { it.length == 6 }?.toIntOrNull(16) },
            onValid = onRgb,
        )
        listOf(R.string.palette_red to 16, R.string.palette_green to 8, R.string.palette_blue to 0).forEach { (label, shift) ->
            EntryField(
                shown = ((rgb shr shift) and 0xFF).toString(),
                label = stringResource(label),
                modifier = Modifier.weight(1f),
                keyboard = KeyboardType.Number,
                parse = { t -> t.toIntOrNull()?.takeIf { it in 0..255 } },
                onValid = { channel -> onRgb((rgb and (0xFF shl shift).inv()) or (channel shl shift)) },
            )
        }
    }
}

@Composable
private fun EntryField(
    shown: String,
    label: String,
    modifier: Modifier,
    keyboard: KeyboardType,
    parse: (String) -> Int?,
    onValid: (Int) -> Unit,
) {
    var text by remember(shown) { mutableStateOf(shown) }
    OutlinedTextField(
        value = text,
        onValueChange = { t ->
            text = t
            parse(t)?.let(onValid)
        },
        singleLine = true,
        label = { Text(label) },
        keyboardOptions = KeyboardOptions(keyboardType = keyboard),
        modifier = modifier,
    )
}

@Composable
private fun SavedPaletteList(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
    onEdit: (CustomPalette) -> Unit,
) {
    Text(stringResource(R.string.palette_saved_heading), style = MaterialTheme.typography.labelSmall)
    palettes.items.forEach { palette ->
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(palette.name, style = MaterialTheme.typography.labelMedium)
                GradientPreview(palette.stops, modifier = Modifier.fillMaxWidth().padding(end = 8.dp))
            }
            TextButton(onClick = { onChange(PaletteStore.applyPalette(p, palette)) }) {
                Text(stringResource(R.string.palette_apply), style = MaterialTheme.typography.labelSmall)
            }
            TextButton(onClick = { onEdit(palette) }) {
                Text(stringResource(R.string.palette_edit), style = MaterialTheme.typography.labelSmall)
            }
            if (!palette.builtIn) {
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

/**
 * Eyedropper: a see-through window over the app; one tap on the visual asks the renderer for the colour of its
 * finished frame at that point (a one-pixel read on the GL thread, answered a frame or two later) and hands it back.
 */
@Composable
private fun EyedropperOverlay(
    onPicked: (Int) -> Unit,
    onCancel: () -> Unit,
) {
    val host = LocalView.current
    var tap by remember { mutableStateOf<Offset?>(null) }
    Dialog(
        onDismissRequest = onCancel,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        val view = LocalView.current
        SideEffect {
            (view.parent as? DialogWindowProvider)?.window?.apply {
                clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND)
                setDimAmount(0f)
            }
        }
        Box(
            Modifier
                .fillMaxSize()
                .pointerInput(Unit) {
                    detectTapGestures { at ->
                        // The tap in screen pixels, then as a fraction of the app window the visual fills.
                        val shown = IntArray(2).also(view::getLocationOnScreen)
                        val window = IntArray(2).also(host::getLocationOnScreen)
                        tap =
                            Offset(
                                (shown[0] + at.x - window[0]) / host.width.coerceAtLeast(1),
                                (shown[1] + at.y - window[1]) / host.height.coerceAtLeast(1),
                            )
                    }
                },
        ) {
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.9f),
                modifier = Modifier.align(Alignment.TopCenter).padding(top = 56.dp),
            ) {
                Row(Modifier.padding(start = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.palette_eyedropper_hint), style = MaterialTheme.typography.labelLarge)
                    TextButton(onClick = onCancel, modifier = Modifier.width(96.dp)) { Text(stringResource(R.string.palette_cancel)) }
                }
            }
        }
    }
    LaunchedEffect(tap) {
        val at = tap ?: return@LaunchedEffect
        NativeViz.probeColorAt(at.x, at.y)
        repeat(PROBE_POLLS) {
            val color = NativeViz.takeProbedColor()
            if (color >= 0) {
                onPicked(color)
                return@LaunchedEffect
            }
            delay(PROBE_POLL_MS)
        }
        onCancel()
    }
}

private const val PROBE_POLLS = 40
private const val PROBE_POLL_MS = 50L
