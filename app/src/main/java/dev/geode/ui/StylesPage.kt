package dev.geode.ui

import android.content.Context
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
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
import dev.geode.render.VisualizerView
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.SceneKind
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.pro.ProBadge
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
 * Visuals › Styles: every style as a picture, not a line of text.
 *
 * Two hundred and more names in nine sub-tabs gave a music lover nothing to go on but words
 * like "Truchet". Here each style is a card with a still of it, and one row of filter chips (the
 * user's own Favourites and Recent first, then each kind and each family) replaces the
 * sub-tabs. A search box narrows the grid by name, kind or family.
 *
 * A tap previews: the stage shows the style at once, nothing is chosen yet, and a bar offers
 * the check that keeps it (CapCut's preview-then-commit). Leaving the page without the check
 * puts the chosen style back. Pro styles preview for everyone; the check is where the paywall
 * stands, since seeing a style move to your own music is the best case for it. A second tap
 * on the previewed card is the check too. A long press stars a style; TalkBack gets the same as a
 * custom action. The stage's swipe still steps through styles and chooses at once.
 */
@Composable
internal fun StylesTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenTextures: () -> Unit,
) {
    val context = LocalContext.current
    val shelfStore = remember { StyleShelfStore.get(context) }
    val shelf by shelfStore.shelf.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    var filterKey by rememberSaveable { mutableStateOf(FILTER_ALL) }
    var query by rememberSaveable { mutableStateOf("") }
    var previewId by remember { mutableStateOf<String?>(null) }
    // The style in use, which the stage stops showing while another is previewed.
    var chosenId by remember { mutableStateOf(viz.sceneId) }
    val entries = remember { styleEntries() }
    val filters = remember(entries) { styleFilters(entries) }
    val filter = filters.firstOrNull { filterKeyOf(it) == filterKey } ?: StyleFilter.All
    // Anything else that changes the style (the stage swipe, a preset, auto-visuals) ends the preview.
    LaunchedEffect(viz.sceneId) {
        if (previewId != viz.sceneId) {
            previewId = null
            chosenId = viz.sceneId
        }
    }
    // Leaving the page without the check is a "no": the chosen style comes back.
    DisposableEffect(Unit) { onDispose { viewModel.cancelScenePreview() } }
    val cancel: () -> Unit = {
        previewId = null
        viewModel.cancelScenePreview()
    }
    // The style whose check opened the paywall. The session chooses it once a purchase lets it
    // through (PlayerSessionProvider); here it only closes the preview bar and files it in Recent.
    var unlocking by remember { mutableStateOf<String?>(null) }
    val commit: (String) -> Unit = { id ->
        viewModel.selectScene(id)
        if (gate.allowsStyle(id)) {
            shelfStore.pushRecent(id)
            previewId = null
            unlocking = null
        } else {
            unlocking = id
        }
    }
    LaunchedEffect(gate) {
        val id = unlocking ?: return@LaunchedEffect
        if (gate.allowsStyle(id)) {
            unlocking = null
            shelfStore.pushRecent(id)
            if (previewId == id) previewId = null
            chosenId = id
        }
    }
    Column(Modifier.fillMaxSize()) {
        StyleSearchField(query) { query = it }
        LazyRow(
            contentPadding = PaddingValues(horizontal = GeodeSpacing.L),
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            items(filters, key = ::filterKeyOf) { f ->
                FilterChip(
                    selected = f == filter,
                    onClick = { filterKey = filterKeyOf(f) },
                    label = { Text(filterLabel(f)) },
                    modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
                )
            }
        }
        Box(Modifier.weight(1f)) {
            if (filter == StyleFilter.Kind(KIND_MILKDROP) && query.isBlank()) {
                MilkDropTab(viewModel, visualizerView, onOpenTextures)
            } else {
                val shown = remember(entries, filter, shelf, query) { StyleFiltering.apply(entries, filter, shelf, query) }
                if (shown.isEmpty()) {
                    EmptyStyles(filter, query)
                } else {
                    LazyVerticalGrid(
                        columns = GridCells.Adaptive(CARD_MIN_WIDTH),
                        contentPadding = PaddingValues(GeodeSpacing.M),
                        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
                        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
                        modifier = Modifier.fillMaxSize(),
                    ) {
                        items(shown, key = { it.id }) { entry ->
                            StyleCard(
                                entry = entry,
                                chosen = entry.id == chosenId,
                                previewing = entry.id == previewId,
                                locked = !gate.allowsStyle(entry.id),
                                favourite = shelf.isFavourite(entry.id),
                                onTap = {
                                    when (entry.id) {
                                        previewId -> commit(entry.id)
                                        chosenId -> if (previewId != null) cancel()
                                        else -> {
                                            previewId = entry.id
                                            viewModel.previewScene(entry.id)
                                        }
                                    }
                                },
                                onToggleFavourite = { shelfStore.toggleFavourite(entry.id) },
                            )
                        }
                    }
                }
            }
        }
        val previewing = previewId
        AnimatedVisibility(visible = previewing != null) {
            if (previewing != null) {
                PreviewBar(
                    name = sceneDisplayLabel(previewing),
                    locked = !gate.allowsStyle(previewing),
                    onCancel = cancel,
                    onUse = { commit(previewing) },
                )
            }
        }
    }
}

@Composable
private fun StyleSearchField(
    query: String,
    onQuery: (String) -> Unit,
) {
    OutlinedTextField(
        value = query,
        onValueChange = onQuery,
        modifier = Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.XS),
        singleLine = true,
        placeholder = { Text("Search styles") },
        leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
        trailingIcon = {
            if (query.isNotEmpty()) {
                IconButton(onClick = { onQuery("") }) { Icon(Icons.Filled.Close, contentDescription = "Clear search") }
            }
        },
        shape = RoundedCornerShape(GeodeRadius.ExtraLarge),
    )
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun StyleCard(
    entry: StyleEntry,
    chosen: Boolean,
    previewing: Boolean,
    locked: Boolean,
    favourite: Boolean,
    onTap: () -> Unit,
    onToggleFavourite: () -> Unit,
) {
    val view = LocalView.current
    val cs = MaterialTheme.colorScheme
    val shape = RoundedCornerShape(GeodeRadius.Medium)
    val favouriteAction = if (favourite) "Remove from favourites" else "Add to favourites"
    val state =
        when {
            previewing -> "Previewing. Tap again to use"
            chosen -> "In use"
            else -> null
        }
    Column(
        Modifier
            .clip(shape)
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
                state?.let { stateDescription = it }
                customActions =
                    listOf(
                        CustomAccessibilityAction(favouriteAction) {
                            onToggleFavourite()
                            true
                        },
                    )
            },
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(shape)
                .then(
                    when {
                        previewing -> Modifier.border(2.dp, cs.tertiary, shape)
                        chosen -> Modifier.border(2.dp, cs.primary, shape)
                        else -> Modifier
                    },
                ),
        ) {
            StyleThumbnail(entry, Modifier.fillMaxSize())
            if (locked) ProBadge(Modifier.align(Alignment.TopEnd).padding(GeodeSpacing.XS))
            if (favourite) {
                Icon(
                    Icons.Filled.Favorite,
                    contentDescription = null,
                    tint = Color.White,
                    modifier =
                        Modifier
                            .align(Alignment.TopStart)
                            .padding(GeodeSpacing.XS)
                            .background(cs.scrim.copy(alpha = SCRIM_ALPHA), RoundedCornerShape(50))
                            .padding(GeodeSpacing.XS)
                            .size(14.dp),
                )
            }
            if (chosen || previewing) {
                Icon(
                    Icons.Filled.Check,
                    contentDescription = null,
                    tint = if (previewing) cs.onTertiary else cs.onPrimary,
                    modifier =
                        Modifier
                            .align(Alignment.BottomEnd)
                            .padding(GeodeSpacing.XS)
                            .background(if (previewing) cs.tertiary else cs.primary, RoundedCornerShape(50))
                            .padding(GeodeSpacing.XXS)
                            .size(16.dp),
                )
            }
        }
        Text(
            entry.label,
            Modifier.padding(top = GeodeSpacing.XS, start = GeodeSpacing.XXS, end = GeodeSpacing.XXS),
            style = MaterialTheme.typography.bodyMedium,
            color = if (chosen || previewing) accentTextColor() else cs.onSurface,
            maxLines = 2,
            minLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * The bar under the grid while a style is previewed. The check is the only way a preview
 * becomes the style, so it is a full-size button with a name that says so, never an icon alone.
 */
@Composable
private fun PreviewBar(
    name: String,
    locked: Boolean,
    onCancel: () -> Unit,
    onUse: () -> Unit,
) {
    Surface(color = MaterialTheme.colorScheme.surfaceContainerHigh, tonalElevation = 3.dp) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("Previewing", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(name, style = MaterialTheme.typography.bodyLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            TextButton(onClick = onCancel, modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget)) { Text("Cancel") }
            CrystalButton(onClick = onUse) {
                Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(18.dp))
                Text(if (locked) "Unlock with Pro" else "Use style", Modifier.padding(start = GeodeSpacing.XS))
            }
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
 */
@Composable
private fun StyleThumbnail(
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

/** Every catalogue style as the grid lists it: loose shader styles, then each kind, then the families. */
private fun styleEntries(): List<StyleEntry> =
    Catalogue.styles.map { style ->
        val family = StyleFamilies.of(style.id)
        StyleEntry(
            id = style.id,
            label = sceneDisplayLabel(style.id),
            kindLabel = if (family != null) KIND_FAMILY else kindLabel(style.kind),
            familyLabel = family?.label,
        )
    }

private fun kindLabel(kind: SceneKind): String =
    when (kind) {
        SceneKind.SHADER -> "Shaders"
        SceneKind.SILK -> "Silk"
        SceneKind.LIFE -> "Life"
        SceneKind.MYCELIUM -> "Mycelium"
        SceneKind.ACID -> "Acid"
        SceneKind.MILKDROP -> KIND_MILKDROP
        SceneKind.FLUID, SceneKind.CURL_FLOW, SceneKind.WATER -> "Fluid"
        SceneKind.CYMATICS -> "Cymatics"
    }

private fun styleFilters(entries: List<StyleEntry>): List<StyleFilter> =
    listOf(StyleFilter.All, StyleFilter.Favourites, StyleFilter.Recent) +
        entries
            .map { it.kindLabel }
            .distinct()
            .filterNot { it == KIND_FAMILY }
            .map { StyleFilter.Kind(it) } +
        StyleFamilies.all.map { StyleFilter.Family(it.label) }

private fun filterKeyOf(filter: StyleFilter): String =
    when (filter) {
        StyleFilter.All -> FILTER_ALL
        StyleFilter.Favourites -> "fav"
        StyleFilter.Recent -> "recent"
        is StyleFilter.Kind -> "kind:${filter.label}"
        is StyleFilter.Family -> "family:${filter.label}"
    }

private fun filterLabel(filter: StyleFilter): String =
    when (filter) {
        StyleFilter.All -> "All"
        StyleFilter.Favourites -> "Favourites"
        StyleFilter.Recent -> "Recent"
        is StyleFilter.Kind -> filter.label
        is StyleFilter.Family -> filter.label
    }

internal fun sceneDisplayLabel(id: String): String {
    val catalogued = VisualStyleCatalog.label(id)
    if (catalogued != id) return catalogued
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

private const val FILTER_ALL = "all"
private const val KIND_MILKDROP = "MilkDrop"
private const val KIND_FAMILY = "Family"
private val CARD_MIN_WIDTH = 104.dp
private const val SCRIM_ALPHA = 0.55f
private const val HUE_TURN = 360
private const val HUE_NUDGE = 40
private const val HUE_SPREAD = 50
private const val FALLBACK_SAT = 0.65f
private const val FALLBACK_DARK = 0.30f
private const val FALLBACK_LIGHT = 0.75f
