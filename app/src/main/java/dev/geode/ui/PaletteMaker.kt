package dev.geode.ui

import android.graphics.Bitmap
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Colorize
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** One lane for every palette read and write, so a save, a delete and the list after them stay in order. */
@OptIn(ExperimentalCoroutinesApi::class)
private val paletteIo: CoroutineDispatcher = Dispatchers.IO.limitedParallelism(1)

/**
 * The saved palettes a Customize control shows. Nothing here touches the disk on the caller's
 * thread: the store is opened (it migrates old ids on open), listed and written in [io], and
 * [items] is empty until [load] has answered.
 */
@Stable
internal class SavedPalettes(
    private val scope: CoroutineScope,
    private val io: CoroutineDispatcher = paletteIo,
    openStore: () -> PaletteStore,
) {
    /** Opened by its first use, which is always inside [io]. */
    private val store by lazy(openStore)

    var items: List<CustomPalette> by mutableStateOf(emptyList())
        private set

    suspend fun load() {
        items = withContext(io) { store.list() }
    }

    /** The palette as it will be stored, at once; the file and the new list follow from [io]. */
    fun save(palette: CustomPalette): CustomPalette {
        val clean = PaletteStore.sanitized(palette)
        write { store.save(clean) }
        return clean
    }

    fun delete(id: String) = write { store.delete(id) }

    /**
     * Started inside the tap and not cancellable once queued: the control that asked can leave the
     * composition (its section closes) before [io] runs, and the user's save must still be written.
     */
    private fun write(edit: () -> Unit) {
        scope.launch(start = CoroutineStart.UNDISPATCHED) {
            items =
                withContext(io + NonCancellable) {
                    edit()
                    store.list()
                }
        }
    }
}

@Composable
internal fun rememberSavedPalettes(): SavedPalettes {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val palettes = remember(context) { SavedPalettes(scope) { PaletteStore(context) } }
    LaunchedEffect(palettes) { palettes.load() }
    return palettes
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

private const val SWATCH_SAMPLES = 24

/** An opaque Compose colour from a packed 0xRRGGBB. */
internal fun packedColour(rgb: Int): Color = Color(OPAQUE or rgb)

private const val OPAQUE = 0xFF000000.toInt()

/** What a slot of [p] paints, before Hue shift (native: its stops, else its hue ramp). */
internal fun slotSwatch(
    p: SceneParams,
    second: Boolean = false,
): List<Color> = GeodeNative.paramsPalettePreview(SceneParamsCodec.pack(p), second, SWATCH_SAMPLES).map(::packedColour)

private fun savedSwatch(palette: CustomPalette): List<Color> = slotSwatch(PaletteStore.applyPalette(SceneParams.DEFAULT, palette))

private fun stopsSwatch(stops: List<Int>): List<Color> = GeodeNative.paletteRamp(stops.toIntArray(), SWATCH_SAMPLES).map(::packedColour)

@Composable
internal fun PaletteSwatch(
    colours: List<Color>,
    modifier: Modifier = Modifier,
) {
    val brush = if (colours.size > 1) Brush.horizontalGradient(colours) else Brush.horizontalGradient(colours + colours)
    Box(modifier.height(18.dp).clip(RoundedCornerShape(4.dp)).background(brush))
}

/**
 * The one palette list (UI V2 rule 2: a dropdown, not a chip wall): the built-in palettes, then the user's
 * own, with what the first slot paints under it. The blend's second slot steps through the built-ins
 * beside it ([SecondPaletteStepper]) instead of showing the list again.
 */
@Composable
internal fun PaletteDropdown(
    lockKey: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    val saved = palettes.items
    Column {
        DropdownRow(
            name = lockKey,
            options = SceneParams.PALETTES + saved.map { it.name },
            selectedIndex = paletteChipIndex(p, saved, second = false),
            lockKey = lockKey,
            placeholder = stringResource(R.string.look_summary_custom),
        ) { i -> onChange(paletteChipSelected(p, saved, i, second = false)) }
        PaletteSwatch(slotSwatch(p), Modifier.fillMaxWidth())
    }
}

/** The blend's second palette: ‹ name › over the built-ins, and what it paints. */
@Composable
internal fun SecondPaletteStepper(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val names = SceneParams.PALETTES
    val label = if (p.usesCustomPalette2) stringResource(R.string.look_summary_custom) else names.getOrElse(p.palette2) { "" }
    val step = { by: Int -> onChange(PaletteStore.clear(p.copy(palette2 = Math.floorMod(p.palette2 + by, names.size)), second = true)) }
    Column {
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.palette_second), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            IconButton(onClick = { step(-1) }) {
                Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = stringResource(R.string.palette_second_previous))
            }
            Text(label, style = MaterialTheme.typography.bodyMedium)
            IconButton(onClick = { step(1) }) {
                Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = stringResource(R.string.palette_second_next))
            }
        }
        PaletteSwatch(slotSwatch(p, second = true), Modifier.fillMaxWidth())
    }
}

/** The stops a look starts the editor on: its own colours, else the palette it paints as four stops. */
private fun startingStops(p: SceneParams): List<Int> =
    p.paletteStops.ifEmpty { GeodeNative.paletteStopsFromHues(p.paletteBase, p.paletteRange, STARTING_STOPS).toList() }

private const val STARTING_STOPS = 4

/**
 * Your colours (Customize › Look, Pro): up to five colour stops made the Photoshop way, a square and a
 * hue strip, a hex field and an eyedropper on the live visual or the artwork. Every change plays on the
 * visual at once (the stops are scene params, so the wallpaper, dream, Cast, export and presets carry
 * them as they carry any palette); Save names them and lists them beside the built-ins.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun YourColoursCard(
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    var stops by remember { mutableStateOf(startingStops(p)) }
    var selected by remember { mutableIntStateOf(0) }
    var name by rememberSaveable { mutableStateOf("") }
    var picking by remember { mutableStateOf<Bitmap?>(null) }
    val view = LocalView.current
    val scope = rememberCoroutineScope()
    val commit = { next: List<Int> ->
        stops = next
        selected = selected.coerceIn(0, next.lastIndex)
        onChange(PaletteStore.applyStops(p, next))
    }
    val setSelected = { rgb: Int -> commit(stops.toMutableList().also { it[selected] = rgb }) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(stringResource(R.string.palette_explainer), style = MaterialTheme.typography.labelSmall)
        PaletteSwatch(stopsSwatch(stops), Modifier.fillMaxWidth())
        StopsRow(stops, selected, onSelect = { selected = it }, onAdd = {
            commit(stops + stops[selected])
            selected = stops.lastIndex
        }, onRemove = { commit(stops.filterIndexed { i, _ -> i != selected }) })
        ColourPicker(rgb = stops[selected], onChange = setSelected)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = {
                host.visualizerView.visualizerRenderer.nativeViz
                    .requestStill { still -> view.post { picking = still } }
            }) {
                Icon(Icons.Filled.Colorize, contentDescription = null)
                Text(stringResource(R.string.palette_pick_visual), Modifier.padding(start = 6.dp))
            }
            OutlinedButton(onClick = { scope.launch { host.viewModel.artworkForPicking()?.let { picking = it } } }) {
                Icon(Icons.Filled.Colorize, contentDescription = null)
                Text(stringResource(R.string.palette_pick_artwork), Modifier.padding(start = 6.dp))
            }
            TextButton(onClick = { commit(GeodeNative.paletteStopsFromHues(p.paletteBase, p.paletteRange, STARTING_STOPS).toList()) }) {
                Text(stringResource(R.string.palette_from_current))
            }
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
                val stored = palettes.save(PaletteStore.create(name, stops))
                name = ""
                onChange(PaletteStore.applyPalette(p, stored))
            },
            enabled = name.isNotBlank(),
        ) { Text(stringResource(R.string.palette_save)) }
        SavedPaletteList(palettes, onEdit = { palette ->
            name = palette.name
            selected = 0
            commit(palette.stops.ifEmpty { GeodeNative.paletteStopsFromHues(palette.baseHue, palette.hueSpan, STARTING_STOPS).toList() })
        }, onDelete = { palette ->
            palettes.delete(palette.id)
            onChange(PaletteStore.forgetDeleted(p, palette.id))
        })
    }
    picking?.let { image ->
        EyedropperDialog(image, onPick = { rgb ->
            picking = null
            setSelected(rgb)
        }, onDismiss = { picking = null })
    }
}

@Composable
private fun StopsRow(
    stops: List<Int>,
    current: Int,
    onSelect: (Int) -> Unit,
    onAdd: () -> Unit,
    onRemove: () -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        stops.forEachIndexed { i, rgb ->
            val label = stringResource(R.string.palette_stop, i + 1, stops.size, GeodeNative.colourHex(rgb))
            Box(
                Modifier
                    .size(GeodeSpacing.TouchTarget)
                    .clickable(role = Role.Tab) { onSelect(i) }
                    .semantics {
                        contentDescription = label
                        selected = i == current
                    },
                contentAlignment = Alignment.Center,
            ) {
                val ring = if (i == current) MaterialTheme.colorScheme.onSurface else Color.Transparent
                Box(
                    Modifier
                        .size(34.dp)
                        .border(2.dp, ring, CircleShape)
                        .padding(4.dp)
                        .clip(CircleShape)
                        .background(packedColour(rgb)),
                )
            }
        }
        Spacer(Modifier.weight(1f))
        IconButton(onClick = onRemove, enabled = stops.size > 1) {
            Icon(Icons.Filled.Remove, contentDescription = stringResource(R.string.palette_stop_remove))
        }
        IconButton(onClick = onAdd, enabled = stops.size < PaletteStore.MAX_STOPS) {
            Icon(Icons.Filled.Add, contentDescription = stringResource(R.string.palette_stop_add))
        }
    }
}

@Composable
private fun SavedPaletteList(
    palettes: SavedPalettes,
    onEdit: (CustomPalette) -> Unit,
    onDelete: (CustomPalette) -> Unit,
) {
    Text(
        stringResource(R.string.palette_saved_heading),
        style = MaterialTheme.typography.labelSmall,
        modifier = Modifier.semantics { heading() },
    )
    if (palettes.items.isEmpty()) {
        Text(stringResource(R.string.palette_none_yet), style = MaterialTheme.typography.labelSmall)
    }
    palettes.items.forEach { palette ->
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(palette.name, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            PaletteSwatch(savedSwatch(palette), Modifier.width(64.dp))
            TextButton(onClick = { onEdit(palette) }) {
                Text(stringResource(R.string.palette_edit), style = MaterialTheme.typography.labelSmall)
            }
            TextButton(onClick = { onDelete(palette) }) {
                Text(stringResource(R.string.palette_delete), style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}
