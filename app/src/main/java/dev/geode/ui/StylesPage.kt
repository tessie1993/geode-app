package dev.geode.ui

import android.content.Context
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.FavoriteBorder
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.WorkspacePremium
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.data.StyleShelfStore
import dev.geode.model.StyleEntry
import dev.geode.model.StyleFilter
import dev.geode.model.StyleFiltering
import dev.geode.model.StyleLook
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.SceneKind
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.StyleFamily
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import kotlin.math.abs

/**
 * Visuals › Styles: every style as a row with a still of it, in groups by what it looks like.
 *
 * A dropdown at the top picks the group ([StyleLook]: Flowing & liquid, Fractals, Space…), or the
 * user's own Favourites and Recent, or everything; the list under it is one row per style: its
 * still, its name, a ♛ when it is Pro, and a heart. Search narrows the list by name, group, kind or
 * family. The owner chose list rows over the old card grid, which showed three styles a screen.
 *
 * A tap loads the style: the visual changes at once, and that is the choice (no "Use style" step).
 * A Pro style a free user taps still plays on the visual while the paywall offers it; leaving the
 * page without buying puts the chosen style back ([PlayerViewModel.leaveScenePreview]). A long
 * press or the heart stars a style; TalkBack gets the same as a custom action. The swipe on the
 * visual still steps through styles in this order.
 */
@Composable
internal fun StylesTab(viewModel: PlayerViewModel) {
    val context = LocalContext.current
    val shelfStore = remember { StyleShelfStore.get(context) }
    val shelf by shelfStore.shelf.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    var filterKey by rememberSaveable { mutableStateOf(FILTER_ALL) }
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    val entries = remember { styleEntries() }
    val filters = remember { styleFilters() }
    val filter = filters.firstOrNull { filterKeyOf(it) == filterKey } ?: StyleFilter.All
    // Leaving the page keeps the style the user may keep; only a locked one gives way.
    DisposableEffect(Unit) { onDispose { viewModel.leaveScenePreview() } }
    // The style whose tap opened the paywall. The session chooses it once a purchase lets it
    // through (PlayerSessionProvider); here it only files it in Recent.
    var unlocking by remember { mutableStateOf<String?>(null) }
    val load: (String) -> Unit = { id ->
        if (gate.allowsStyle(id)) {
            viewModel.selectScene(id)
            shelfStore.pushRecent(id)
            unlocking = null
        } else {
            // Seeing it move to your own music is the best case for Pro: play it, then ask.
            viewModel.previewScene(id)
            viewModel.selectScene(id)
            unlocking = id
        }
    }
    LaunchedEffect(gate) {
        val id = unlocking ?: return@LaunchedEffect
        if (gate.allowsStyle(id)) {
            unlocking = null
            shelfStore.pushRecent(id)
        }
    }
    Column(Modifier.fillMaxSize()) {
        if (searching) {
            StyleSearchField(query, onQuery = { query = it }, onClose = {
                searching = false
                query = ""
            })
        } else {
            Row(
                Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget).padding(horizontal = GeodeSpacing.S),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                StyleGroupMenu(filters, filter, onPick = { filterKey = filterKeyOf(it) }, modifier = Modifier.weight(1f))
                IconButton(onClick = { searching = true }) { Icon(Icons.Filled.Search, contentDescription = "Search styles") }
            }
        }
        val shown = remember(entries, filter, shelf, query) { StyleFiltering.apply(entries, filter, shelf, query) }
        if (shown.isEmpty()) {
            EmptyStyles(filter, query)
        } else {
            LazyColumn(
                contentPadding = PaddingValues(bottom = GeodeSpacing.L),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(shown, key = { it.id }, contentType = { "style" }) { entry ->
                    StyleRow(
                        entry = entry,
                        chosen = entry.id == viz.sceneId,
                        locked = !gate.allowsStyle(entry.id),
                        favourite = shelf.isFavourite(entry.id),
                        onTap = { load(entry.id) },
                        onToggleFavourite = { shelfStore.toggleFavourite(entry.id) },
                    )
                }
            }
        }
    }
}

/** The group picker: the current group's name with ▾, a standard menu of every group under it. */
@Composable
private fun StyleGroupMenu(
    filters: List<StyleFilter>,
    filter: StyleFilter,
    onPick: (StyleFilter) -> Unit,
    modifier: Modifier = Modifier,
) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        TextButton(onClick = { open = true }, modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget)) {
            Text(filterLabel(filter), style = MaterialTheme.typography.titleMedium)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = "Choose a group")
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            filters.forEach { f ->
                DropdownMenuItem(
                    text = { Text(filterLabel(f)) },
                    trailingIcon = if (f == filter) ({ Icon(Icons.Filled.Check, contentDescription = "Shown") }) else null,
                    onClick = {
                        open = false
                        onPick(f)
                    },
                )
                // The user's own shelf above, the look groups below.
                if (f == StyleFilter.Recent) HorizontalDivider()
            }
        }
    }
}

@Composable
private fun StyleSearchField(
    query: String,
    onQuery: (String) -> Unit,
    onClose: () -> Unit,
) {
    OutlinedTextField(
        value = query,
        onValueChange = onQuery,
        modifier = Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.XS),
        singleLine = true,
        placeholder = { Text("Search styles") },
        leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
        trailingIcon = {
            IconButton(onClick = onClose) { Icon(Icons.Filled.Close, contentDescription = "Close search") }
        },
        shape = RoundedCornerShape(GeodeRadius.ExtraLarge),
    )
}

/**
 * One style: its still, its name (lit when it is on screen), a ♛ when it is Pro, and a heart. The
 * row is one 56 dp target; the heart is its own 48 dp button.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun StyleRow(
    entry: StyleEntry,
    chosen: Boolean,
    locked: Boolean,
    favourite: Boolean,
    onTap: () -> Unit,
    onToggleFavourite: () -> Unit,
) {
    val view = LocalView.current
    val cs = MaterialTheme.colorScheme
    val favouriteAction = if (favourite) "Remove from favourites" else "Add to favourites"
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = ROW_HEIGHT)
            .combinedClickable(
                role = Role.Button,
                onClick = onTap,
                onLongClick = {
                    view.performStoneHaptic(StoneHapticCue.CONFIRM)
                    onToggleFavourite()
                },
                onLongClickLabel = favouriteAction,
            ).semantics {
                contentDescription = entry.label + if (locked) ", Pro" else ""
                if (chosen) stateDescription = "On screen"
                customActions =
                    listOf(
                        CustomAccessibilityAction(favouriteAction) {
                            onToggleFavourite()
                            true
                        },
                    )
            }.padding(horizontal = GeodeSpacing.L),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val shape = RoundedCornerShape(GeodeRadius.Small)
        StyleThumbnail(
            entry,
            Modifier
                .size(THUMB_SIZE)
                .clip(shape)
                .then(if (chosen) Modifier.border(2.dp, cs.primary, shape) else Modifier),
        )
        Text(
            entry.label,
            Modifier.weight(1f).padding(horizontal = GeodeSpacing.M),
            style = MaterialTheme.typography.bodyLarge,
            color = if (chosen) accentTextColor() else cs.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        if (locked) {
            Icon(Icons.Filled.WorkspacePremium, contentDescription = null, tint = accentTextColor(), modifier = Modifier.size(20.dp))
        }
        IconButton(onClick = onToggleFavourite) {
            Icon(
                if (favourite) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder,
                contentDescription = favouriteAction,
                tint = if (favourite) accentTextColor() else cs.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun EmptyStyles(
    filter: StyleFilter,
    query: String,
) {
    val text =
        when {
            query.isNotBlank() -> "No style matches \"${query.trim()}\"."
            filter == StyleFilter.Favourites -> "No favourites yet. Press and hold a style to keep it here."
            filter == StyleFilter.Recent -> "Styles you use show up here."
            else -> "Nothing here yet."
        }
    Box(Modifier.fillMaxSize().padding(GeodeSpacing.XL), contentAlignment = Alignment.Center) {
        Text(text, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * The still of a style: `assets/thumbs/<id>.webp`, rendered on CI by
 * `StyleThumbnailRenderTest`. Until a style has one (a new style, or MilkDrop, whose look is
 * the preset's) it gets a gradient keyed to its family or kind, so the grid never shows a hole.
 * Internal because the Presets cards show their style's still too.
 */
@Composable
internal fun StyleThumbnail(
    entry: StyleEntry,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val bitmap by produceState(StyleThumbs.cached(entry.id), entry.id) {
        if (value == null) value = withContext(Dispatchers.IO) { StyleThumbs.load(context, entry.id) }
    }
    val image = bitmap
    if (image != null) {
        Image(image, contentDescription = null, contentScale = ContentScale.Crop, modifier = modifier)
    } else {
        Box(modifier.background(fallbackBrush(entry)))
    }
}

/** Two hues from the family (or kind), nudged per style so neighbours in a family still differ. */
private fun fallbackBrush(entry: StyleEntry): Brush {
    val base = abs((entry.familyLabel ?: entry.kindLabel).hashCode()) % HUE_TURN
    val nudge = abs(entry.id.hashCode()) % HUE_NUDGE
    val a = Color.hsv(((base + nudge) % HUE_TURN).toFloat(), FALLBACK_SAT, FALLBACK_DARK)
    val b = Color.hsv(((base + nudge + HUE_SPREAD) % HUE_TURN).toFloat(), FALLBACK_SAT, FALLBACK_LIGHT)
    return Brush.linearGradient(listOf(a, b))
}

/**
 * Decoded thumbnails, held by bytes so a long scroll cannot hold every one. A style without a
 * file is remembered too, so the grid does not ask the assets again on every bind.
 */
private object StyleThumbs {
    private val cache =
        object : LruCache<String, ImageBitmap>(CACHE_BYTES) {
            override fun sizeOf(
                key: String,
                value: ImageBitmap,
            ): Int = value.width * value.height * BYTES_PER_PIXEL
        }
    private val missing: MutableSet<String> = ConcurrentHashMap.newKeySet()

    fun cached(id: String): ImageBitmap? = cache.get(id)

    fun load(
        context: Context,
        id: String,
    ): ImageBitmap? {
        cache.get(id)?.let { return it }
        if (id in missing) return null
        val bitmap =
            try {
                context.assets.open("thumbs/$id.webp").use { BitmapFactory.decodeStream(it) }
            } catch (_: IOException) {
                null
            }
        if (bitmap == null) {
            missing += id
            return null
        }
        return bitmap.asImageBitmap().also { cache.put(id, it) }
    }

    private const val CACHE_BYTES = 16 * 1024 * 1024
    private const val BYTES_PER_PIXEL = 4
}

/** Every catalogue style as the list shows it, in catalogue order, each with its look group. */
private fun styleEntries(): List<StyleEntry> =
    Catalogue.styles.map { style ->
        val family = StyleFamilies.of(style.id)
        StyleEntry(
            id = style.id,
            label = sceneDisplayLabel(style.id),
            kindLabel = if (family != null) KIND_FAMILY else kindLabel(style.kind),
            familyLabel = family?.label,
            look = StyleLookTags.of(style.id, style.kind, family),
        )
    }

/**
 * Which [StyleLook] group each style is shown in: UI data, the owner's draft to correct.
 *
 * WHY here and not in `model/StyleLook`: the facts the tags key on (a style's family and its
 * renderer kind) are the native catalogue's (ADR 0003), and this page already reads them from
 * [Catalogue] and [StyleFamilies]. Keying on the catalogue's family names and [SceneKind]s, not on
 * hand-written id prefixes, means a style the engine moves between families moves group with it.
 * What stays hand-written is only the taste: which family or kind looks like which group, and the
 * loose shader styles placed one by one in [LOOSE]. A style missing from every rule lands in
 * [StyleLook.PSYCHEDELIC], so a new style always shows somewhere.
 */
private object StyleLookTags {
    /** A family style goes by its family, any other by its kind, a loose shader by [LOOSE]. */
    fun of(
        id: String,
        kind: SceneKind,
        family: StyleFamily?,
    ): StyleLook =
        family?.let { FAMILIES[it.name] }
            ?: KINDS[kind]
            ?: LOOSE[id]
            ?: StyleLook.PSYCHEDELIC

    /** Keyed by [StyleFamily.name], the catalogue's `family|<name>|…` rows. */
    private val FAMILIES: Map<String, StyleLook> =
        mapOf(
            "bulb" to StyleLook.FRACTALS,
            "arbor" to StyleLook.FRACTALS,
            "foam" to StyleLook.FRACTALS,
            "dive" to StyleLook.FLY_THROUGH,
            "terrain" to StyleLook.FLY_THROUGH,
            "nebula" to StyleLook.SPACE,
            "glass" to StyleLook.GLASS,
            "truchet" to StyleLook.PATTERNS,
            "lucid" to StyleLook.PSYCHEDELIC,
            "chrysanthemum" to StyleLook.PSYCHEDELIC,
        )

    /** Every kind but [SceneKind.SHADER], whose styles are placed one by one. */
    private val KINDS: Map<SceneKind, StyleLook> =
        mapOf(
            SceneKind.SILK to StyleLook.FLOWING,
            SceneKind.FLUID to StyleLook.FLOWING,
            SceneKind.CURL_FLOW to StyleLook.FLOWING,
            SceneKind.WATER to StyleLook.FLOWING,
            SceneKind.LIFE to StyleLook.LIVING,
            SceneKind.MYCELIUM to StyleLook.LIVING,
            SceneKind.ACID to StyleLook.PSYCHEDELIC,
            SceneKind.CYMATICS to StyleLook.PATTERNS,
            SceneKind.MILKDROP to StyleLook.CLASSIC,
        )

    private val LOOSE: Map<String, StyleLook> =
        buildMap {
            listOf("julia", "mandel", "kifs", "fractal_temple", "menger_core", "mandelbox_cathedral", "supershape")
                .forEach { put(it, StyleLook.FRACTALS) }
            listOf(
                "tunnel",
                "rod_tunnel",
                "particle_tunnel",
                "fractal_dive",
                "fractal_tunnel",
                "outrun_drive",
                "vanishing",
                "crystal_canyon",
                "spectrum_terrain",
                "gyroid_cavern",
                "noneuclid",
            ).forEach { put(it, StyleLook.FLY_THROUGH) }
            listOf("starfield", "nebula", "solar", "chrome_nebula", "ringed_planet", "black_hole", "aurora", "aurora_ocean")
                .forEach { put(it, StyleLook.SPACE) }
            listOf(
                "orb_lattice",
                "neon_tiles",
                "chroma_orb",
                "blacklight_bloom",
                "prism_rain",
                "glow_vortex",
                "neon_chrome_sea",
                "neon_city",
                "merkaba_grid",
                "bead_vortex",
            ).forEach { put(it, StyleLook.GLASS) }
            listOf("kaleido", "grid", "voronoi", "hexgrid", "spiral", "mandala_dome", "hex_hive", "spiral_eye")
                .forEach { put(it, StyleLook.PATTERNS) }
            listOf("bars", "ring", "scope", "liss", "waves").forEach { put(it, StyleLook.CLASSIC) }
            listOf(
                "plasma",
                "warp",
                "metaballs",
                "lava",
                "morphogen",
                "curl_bloom",
                "nectar_flow",
                "jelly_abyss",
                "ribbon_storm",
                "ripples",
            ).forEach { put(it, StyleLook.FLOWING) }
        }
}

private fun kindLabel(kind: SceneKind): String =
    when (kind) {
        SceneKind.SHADER -> "Shaders"
        SceneKind.SILK -> "Silk"
        SceneKind.LIFE -> "Life"
        SceneKind.MYCELIUM -> "Mycelium"
        SceneKind.ACID -> "Acid"
        SceneKind.MILKDROP -> "MilkDrop"
        SceneKind.FLUID, SceneKind.CURL_FLOW, SceneKind.WATER -> "Fluid"
        SceneKind.CYMATICS -> "Cymatics"
    }

/** Everything, the user's own shelf, then each look group in [StyleLook]'s order. */
private fun styleFilters(): List<StyleFilter> =
    listOf(StyleFilter.All, StyleFilter.Favourites, StyleFilter.Recent) + StyleLook.entries.map { StyleFilter.Look(it) }

private fun filterKeyOf(filter: StyleFilter): String =
    when (filter) {
        StyleFilter.All -> FILTER_ALL
        StyleFilter.Favourites -> "fav"
        StyleFilter.Recent -> "recent"
        is StyleFilter.Kind -> "kind:${filter.label}"
        is StyleFilter.Family -> "family:${filter.label}"
        is StyleFilter.Look -> "look:${filter.look.name}"
    }

private fun filterLabel(filter: StyleFilter): String =
    when (filter) {
        StyleFilter.All -> "All styles"
        StyleFilter.Favourites -> "Favourites"
        StyleFilter.Recent -> "Recent"
        is StyleFilter.Kind -> filter.label
        is StyleFilter.Family -> filter.label
        is StyleFilter.Look -> filter.look.label
    }

internal fun sceneDisplayLabel(id: String): String {
    val catalogued = VisualStyleCatalog.label(id)
    if (catalogued != id) return catalogued
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

private const val FILTER_ALL = "all"
private const val KIND_FAMILY = "Family"
private val ROW_HEIGHT = 56.dp
private val THUMB_SIZE = 40.dp
private const val HUE_TURN = 360
private const val HUE_NUDGE = 40
private const val HUE_SPREAD = 50
private const val FALLBACK_SAT = 0.65f
private const val FALLBACK_DARK = 0.30f
private const val FALLBACK_LIGHT = 0.75f
