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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.data.CustomPalette
import dev.geode.data.PaletteStore
import dev.geode.engine.bridge.GeodeNative
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

/** An opaque Compose colour from a packed 0xRRGGBB. */
internal fun packedColour(rgb: Int): Color = Color(OPAQUE or rgb)

private const val OPAQUE = 0xFF000000.toInt()

/** Native's strip ("RRGGBB" joined by ',') as the colours a [PaletteSwatch] draws. */
internal fun swatchColours(text: String): List<Color> =
    text
        .split(',')
        .filter { it.isNotEmpty() }
        .map { packedColour(it.toInt(HEX)) }

private const val HEX = 16

/** A native row's fields, one row a line. */
internal fun nativeRows(text: String): List<List<String>> = text.lines().filter { it.isNotEmpty() }.map { it.split('|') }

@Composable
internal fun PaletteSwatch(
    colours: List<Color>,
    modifier: Modifier = Modifier,
) {
    val brush = if (colours.size > 1) Brush.horizontalGradient(colours) else Brush.horizontalGradient(colours + colours)
    Box(modifier.height(18.dp).clip(RoundedCornerShape(4.dp)).background(brush))
}

/**
 * The one palette list on the styles the stack does not draw (UI V2 rule 2: a dropdown, not a chip wall):
 * the built-in palettes and the user's own as native lists them, with what the look's palette paints under it.
 */
@Composable
internal fun PaletteDropdown(
    lockKey: String,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    val saved = palettes.items
    val rows =
        remember(p, saved) {
            val ids = saved.joinToString("\n") { it.id }
            nativeRows(GeodeNative.paletteListView(p.frame(), p.customPaletteId, ids, saved.joinToString("\n") { it.name }))
        }
    val head = rows.first()
    val options = rows.drop(1)
    Column {
        DropdownRow(
            name = lockKey,
            options = options.map { it[3] },
            selectedIndex = head[2].toInt(),
            lockKey = lockKey,
            placeholder = head[3],
        ) { i ->
            // Native names the pick: a built-in by its index, else one of the saved palettes by its place.
            val own = options[i][2].toInt()
            val picked = if (own < 0) p.with("palette", options[i][1].toFloat()).withoutCustomPalette() else p.withSavedPalette(saved[own])
            onChange(picked)
        }
        PaletteSwatch(swatchColours(head[4]), Modifier.fillMaxWidth())
    }
}

/**
 * Your colours (Customize › Look, Pro): up to five colour stops made the Photoshop way, a square and a
 * hue strip, a hex field and an eyedropper on the live visual or the artwork. Every change plays on the
 * visual at once (the stops are scene params, so the wallpaper, dream, Cast, export and presets carry
 * them as they carry any palette); Save names them and lists them beside the built-ins. The editor is
 * native's (core/viz/PalettePanel): the stops, the selected one, the limits and the words; this draws it.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun YourColoursCard(
    host: AdvancedHost,
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
    palettes: SavedPalettes,
) {
    var state by remember { mutableStateOf(GeodeNative.paletteMakerStart(p.frame())) }
    var name by rememberSaveable { mutableStateOf("") }
    var picking by remember { mutableStateOf<Bitmap?>(null) }
    val view = LocalView.current
    val scope = rememberCoroutineScope()
    val rows = remember(state) { nativeRows(GeodeNative.paletteMakerView(state)) }
    val head = rows.first()
    val stops = rows.drop(1)
    val commit = { next: String ->
        state = next
        onChange(p.withStops(GeodeNative.paletteMakerStops(next).toList(), ""))
    }
    val edit = { op: Int, arg: Int -> commit(GeodeNative.paletteMakerEdit(state, op, arg)) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(panelText("palette_explainer"), style = MaterialTheme.typography.labelSmall)
        PaletteSwatch(swatchColours(head[5]), Modifier.fillMaxWidth())
        StopsRow(
            stops,
            removable = head[3] == "1",
            addable = head[4] == "1",
            onSelect = { state = GeodeNative.paletteMakerEdit(state, GeodeNative.PALETTE_MAKER_SELECT, it) },
            onAdd = { edit(GeodeNative.PALETTE_MAKER_ADD, 0) },
            onRemove = { edit(GeodeNative.PALETTE_MAKER_REMOVE, 0) },
        )
        ColourPicker(rgb = stops[head[2].toInt()][2].toInt(HEX), onChange = { edit(GeodeNative.PALETTE_MAKER_SET, it) })
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = {
                host.visualizerView.stage
                    .requestStill { still -> view.post { picking = still } }
            }) {
                Icon(Icons.Filled.Colorize, contentDescription = null)
                Text(panelText("palette_pick_visual"), Modifier.padding(start = 6.dp))
            }
            OutlinedButton(onClick = { scope.launch { host.viewModel.artworkForPicking()?.let { picking = it } } }) {
                Icon(Icons.Filled.Colorize, contentDescription = null)
                Text(panelText("palette_pick_artwork"), Modifier.padding(start = 6.dp))
            }
            TextButton(onClick = { commit(GeodeNative.paletteMakerFromLook(p.frame())) }) {
                Text(panelText("palette_from_current"))
            }
        }
        OutlinedTextField(
            value = name,
            onValueChange = { name = it },
            singleLine = true,
            label = { Text(panelText("palette_name_label")) },
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedButton(
            onClick = {
                val stored = palettes.save(PaletteStore.create(name, GeodeNative.paletteMakerStops(state).toList()))
                name = ""
                onChange(p.withSavedPalette(stored))
            },
            enabled = name.isNotBlank(),
        ) { Text(panelText("palette_save")) }
        SavedPaletteList(palettes, onEdit = { palette ->
            name = palette.name
            commit(GeodeNative.paletteMakerOpen(palette.stops.toIntArray(), palette.baseHue, palette.hueSpan))
        }, onDelete = { palette ->
            palettes.delete(palette.id)
            onChange(p.forgetPalette(palette.id))
        })
    }
    picking?.let { image ->
        EyedropperDialog(image, onPick = { rgb ->
            picking = null
            edit(GeodeNative.PALETTE_MAKER_SET, rgb)
        }, onDismiss = { picking = null })
    }
}

/** The stops as native lists them (`stop|<index>|<RRGGBB>|<selected>|<label>`), then remove and add. */
@Composable
private fun StopsRow(
    stops: List<List<String>>,
    removable: Boolean,
    addable: Boolean,
    onSelect: (Int) -> Unit,
    onAdd: () -> Unit,
    onRemove: () -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        stops.forEach { f ->
            val on = f[3] == "1"
            Box(
                Modifier
                    .size(GeodeSpacing.TouchTarget)
                    .clickable(role = Role.Tab) { onSelect(f[1].toInt()) }
                    .semantics {
                        contentDescription = f[4]
                        selected = on
                    },
                contentAlignment = Alignment.Center,
            ) {
                val ring = if (on) MaterialTheme.colorScheme.onSurface else Color.Transparent
                Box(
                    Modifier
                        .size(34.dp)
                        .border(2.dp, ring, CircleShape)
                        .padding(4.dp)
                        .clip(CircleShape)
                        .background(packedColour(f[2].toInt(HEX))),
                )
            }
        }
        Spacer(Modifier.weight(1f))
        IconButton(onClick = onRemove, enabled = removable) {
            Icon(Icons.Filled.Remove, contentDescription = panelText("palette_stop_remove"))
        }
        IconButton(onClick = onAdd, enabled = addable) {
            Icon(Icons.Filled.Add, contentDescription = panelText("palette_stop_add"))
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
        panelText("palette_saved_heading"),
        style = MaterialTheme.typography.labelSmall,
        modifier = Modifier.semantics { heading() },
    )
    if (palettes.items.isEmpty()) {
        Text(panelText("palette_none_yet"), style = MaterialTheme.typography.labelSmall)
    }
    palettes.items.forEach { palette ->
        val swatch =
            remember(palette) {
                swatchColours(GeodeNative.paletteSavedSwatch(palette.stops.toIntArray(), palette.baseHue, palette.hueSpan))
            }
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Text(palette.name, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            PaletteSwatch(swatch, Modifier.width(64.dp))
            TextButton(onClick = { onEdit(palette) }) {
                Text(panelText("palette_edit"), style = MaterialTheme.typography.labelSmall)
            }
            TextButton(onClick = { onDelete(palette) }) {
                Text(panelText("palette_delete"), style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}
