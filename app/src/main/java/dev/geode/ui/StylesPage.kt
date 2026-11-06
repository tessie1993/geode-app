package dev.geode.ui

import android.content.Context
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.StyleShelfStore
import dev.geode.model.StyleEntry
import dev.geode.model.StyleShelf
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.SceneKind
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import kotlin.math.abs

/**
 * Visuals › Styles: every style in one vertical menu that opens level by level, family › style ›
 * variation (owner, 2026-11-06: no stills, no group dropdown, no search).
 *
 * - **Family**, the engine that draws it: Silk, Life, Mycelium, Acid, Shaders, MilkDrop, Fluid,
 *   Cymatics, in catalogue order ([Catalogue]); a family of one style is a plain row.
 * - **Style**: a shader family (Arbor, Nebula, Truchet…) is one style whose twelve looks are its
 *   variations; every other style is its own row and loads on a tap.
 * - **Variation**: the shader family's styles, the opener first.
 *
 * On top, always open: the user's own styles (on screen, favourites, recent), with Random as an
 * icon on that heading (owner: an icon, not a whole row), which loads a style the user can keep
 * while there is one, never the one on screen. Headers drop open on a tap, several at once, kept
 * by key for the process; a fresh start opens the path to the style on screen.
 *
 * A tap loads the style: the visual changes at once, and that is the choice (no "Use style" step).
 * A Pro style a free user taps still plays on the visual while the paywall offers it, as a half
 * sheet under the visual (`StylePaywallSheet`) so the preview stays in view. Closing the paywall
 * without buying, or leaving the page, puts the chosen style back ([PlayerViewModel.leaveScenePreview]).
 * The heart is the one-tap favourite; a long press opens the row's menu (favourite, add to the
 * visuals playlist, remove from Recent), and TalkBack gets the same items as custom actions. The
 * swipe on the visual still steps through styles in catalogue order.
 */
@Composable
internal fun StylesTab(viewModel: PlayerViewModel) {
    val context = LocalContext.current
    val shelfStore = remember { StyleShelfStore.get(context) }
    val shelf by shelfStore.shelf.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val paywall: PaywallViewModel = geodeViewModel()
    val paywallState by paywall.uiState.collectAsStateWithLifecycle()
    val paywallOpen = paywallState.visible
    val entries = remember { styleEntries() }
    val tree = remember(entries) { styleTree(entries) }
    var open by remember { mutableStateOf(StylesGroupMemory.open ?: tree.pathTo(viz.sceneId)) }
    val toggle: (String) -> Unit = { key ->
        open = if (key in open) open - key else open + key
        StylesGroupMemory.open = open
    }
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
    // The paywall went away (Close, Back, a swipe or a tap above it) and the style is still locked:
    // the preview ends here, back on the chosen style, not only once the user leaves the page.
    // A purchase keeps it: the gate allows it then, and the session chooses it.
    LaunchedEffect(paywallOpen) {
        val id = unlocking
        if (!paywallOpen && id != null && !gate.allowsStyle(id)) {
            unlocking = null
            viewModel.leaveScenePreview()
        }
    }
    val view = LocalView.current
    val shuffle: () -> Unit = {
        val others = entries.filter { it.id != viz.sceneId }
        // A free user lands on a style they can keep while there is one.
        others.filter { gate.allowsStyle(it.id) }.ifEmpty { others }.randomOrNull()?.let {
            view.performStoneHaptic(StoneHapticCue.CONFIRM)
            load(it.id)
        }
    }
    val row: @Composable (StyleEntry, Int) -> Unit = { entry, depth ->
        StyleRow(
            entry = entry,
            depth = depth,
            chosen = entry.id == viz.sceneId,
            locked = !gate.allowsStyle(entry.id),
            favourite = shelf.isFavourite(entry.id),
            recent = entry.id in shelf.recents,
            onTap = { load(entry.id) },
            onToggleFavourite = { shelfStore.toggleFavourite(entry.id) },
            onAddToPlaylist = {
                viewModel.addToVizPlaylist(VizPlaylistEntry(sceneId = entry.id, label = entry.label))
            },
            onRemoveRecent = { shelfStore.removeRecent(entry.id) },
        )
    }
    LazyColumn(
        contentPadding = PaddingValues(bottom = GeodeSpacing.L),
        modifier = Modifier.fillMaxSize(),
    ) {
        // Your styles: what the user already chose, always open. Keys are prefixed so a style
        // shown here and in the menu below never clashes.
        val mine = yourStyles(entries, shelf, viz.sceneId)
        item(key = "yours", contentType = "overline") { YourStylesHeading(onShuffle = shuffle) }
        if (mine.isEmpty()) {
            item(key = "yours-empty", contentType = "hint") { StylesHint(stringResource(R.string.styles_yours_empty)) }
        }
        items(mine, key = { "mine:${it.id}" }, contentType = { "style" }) { row(it, 0) }
        styleMenu(tree, open, toggle, row)
    }
}

/** The family › style › variation rows, each header followed by its children while open. */
private fun LazyListScope.styleMenu(
    tree: List<StyleNode>,
    open: Set<String>,
    toggle: (String) -> Unit,
    row: @Composable (StyleEntry, Int) -> Unit,
) {
    fun node(
        n: StyleNode,
        depth: Int,
    ) {
        val leaf = n.leaf
        if (leaf != null) {
            item(key = leaf.id, contentType = "style") { row(leaf, depth) }
            return
        }
        val expanded = n.key in open
        item(key = n.key, contentType = "section") {
            StyleSectionHeader(n.label, n.count, depth, expanded) { toggle(n.key) }
        }
        if (expanded) n.children.forEach { node(it, depth + 1) }
    }
    tree.forEach { node(it, 0) }
}

/**
 * A row of the Styles menu: a header with [children], or a [leaf] style that loads on a tap. A
 * header of one child is that child, so a family or style with one look is never a dead level.
 */
private class StyleNode(
    val key: String,
    val label: String,
    val children: List<StyleNode> = emptyList(),
    val leaf: StyleEntry? = null,
) {
    val count: Int get() = leaf?.let { 1 } ?: children.sumOf { it.count }
}

/**
 * Family › style › variation from catalogue order. The family is the engine ([StyleEntry.kindLabel];
 * every shader style, loose or in a shader family, is "Shaders"); inside Shaders the shader
 * families come first, as styles whose variations are their twelve looks, then the loose styles.
 */
private fun styleTree(entries: List<StyleEntry>): List<StyleNode> =
    entries
        .groupBy { if (it.familyLabel != null) SHADERS else it.kindLabel }
        .map { (family, members) ->
            val styles =
                members
                    .groupBy { it.familyLabel ?: it.id }
                    .map { (key, looks) ->
                        if (looks.size == 1) {
                            StyleNode(looks[0].id, looks[0].label, leaf = looks[0])
                        } else {
                            StyleNode("style:$key", looks[0].familyLabel ?: key, looks.map { StyleNode(it.id, it.label, leaf = it) })
                        }
                    }.sortedBy { it.leaf != null }
            styles.singleOrNull() ?: StyleNode("family:$family", family, styles)
        }

/** The headers to open so [id] shows: its family, then its style. */
private fun List<StyleNode>.pathTo(id: String?): Set<String> {
    fun walk(n: StyleNode): List<String>? =
        when {
            n.leaf != null -> if (n.leaf.id == id) emptyList() else null
            else -> n.children.firstNotNullOfOrNull(::walk)?.let { listOf(n.key) + it }
        }
    return firstNotNullOfOrNull(::walk)?.toSet() ?: emptySet()
}

/** The style on screen, then favourites (catalogue order), then recent, each style once. */
private fun yourStyles(
    entries: List<StyleEntry>,
    shelf: StyleShelf,
    onScreen: String?,
): List<StyleEntry> {
    val byId = entries.associateBy { it.id }
    val ids = listOfNotNull(onScreen) + entries.map { it.id }.filter(shelf::isFavourite) + shelf.recents
    return ids.distinct().mapNotNull { byId[it] }
}

/** "Your styles", with Random as the icon at its end (owner: an icon, not a whole row). */
@Composable
private fun YourStylesHeading(onShuffle: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget).padding(start = GeodeSpacing.L, end = GeodeSpacing.XS),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CrystalOverline(stringResource(R.string.styles_yours), Modifier.weight(1f).semantics { heading() })
        IconButton(onClick = onShuffle) {
            StoneIconArt(StoneIcon.SHUFFLE, contentDescription = stringResource(R.string.styles_random), tint = accentTextColor())
        }
    }
}

/**
 * A menu header: its name, how many styles under it, and a chevron that turns over, as Settings'
 * section cards do ([SettingsSectionCard]). Indented by [depth]; a lazy row, not a card around its
 * children, so an open family is still drawn only as it scrolls in.
 */
@Composable
private fun StyleSectionHeader(
    title: String,
    count: Int,
    depth: Int,
    expanded: Boolean,
    onToggle: () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val turn by animateFloatAsState(if (expanded) HALF_TURN else 0f, motion.fastSpatialSpec(), label = "group chevron")
    val stateText = stringResource(if (expanded) R.string.customize_card_expanded else R.string.customize_card_collapsed)
    val actionText = stringResource(if (expanded) R.string.customize_card_collapse else R.string.customize_card_expand)
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .clickable(role = Role.Button, onClickLabel = actionText, onClick = onToggle)
            .semantics(mergeDescendants = true) {
                heading()
                stateDescription = stateText
            }.padding(start = indent(depth), end = GeodeSpacing.M),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title,
            style = if (depth == 0) MaterialTheme.typography.titleSmall else MaterialTheme.typography.bodyMedium,
            modifier = Modifier.weight(1f),
        )
        Text(
            count.toString(),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = GeodeSpacing.S),
        )
        Icon(
            Icons.Filled.ExpandMore,
            contentDescription = null,
            modifier = Modifier.rotate(turn),
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private fun indent(depth: Int): Dp = GeodeSpacing.L + INDENT * depth

@Composable
private fun StylesHint(text: String) {
    Text(
        text,
        Modifier.padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * One style: its name (lit when it is on screen), a ♛ when it is Pro, and a heart, indented by
 * [depth]. The row is one 48 dp target; the heart is its own 48 dp button, the one-tap favourite.
 *
 * A long press opens the row's menu, as on any Android list row: favourite, add to the visuals
 * playlist (not for a locked style, which the playlist would only skip), and remove from Recent
 * when it is there. TalkBack reads the same items as custom actions, so nothing hides behind a hold.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun StyleRow(
    entry: StyleEntry,
    depth: Int,
    chosen: Boolean,
    locked: Boolean,
    favourite: Boolean,
    recent: Boolean,
    onTap: () -> Unit,
    onToggleFavourite: () -> Unit,
    onAddToPlaylist: () -> Unit,
    onRemoveRecent: () -> Unit,
) {
    val view = LocalView.current
    val cs = MaterialTheme.colorScheme
    var menuOpen by remember { mutableStateOf(false) }
    val favouriteAction = stringResource(if (favourite) R.string.styles_favourite_remove else R.string.styles_favourite_add)
    val playlistAction = stringResource(R.string.styles_playlist_add)
    val recentAction = stringResource(R.string.styles_recent_remove)
    val moreOptions = stringResource(R.string.styles_more_options)
    val onScreen = stringResource(R.string.styles_on_screen)
    val menu =
        buildList {
            add(favouriteAction to onToggleFavourite)
            if (!locked) add(playlistAction to onAddToPlaylist)
            if (recent) add(recentAction to onRemoveRecent)
        }
    Box {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .then(if (chosen) Modifier.drawBehind { drawChosenGlass() } else Modifier)
                .combinedClickable(
                    role = Role.Button,
                    onClick = onTap,
                    onLongClick = {
                        view.performStoneHaptic(StoneHapticCue.CONFIRM)
                        menuOpen = true
                    },
                    onLongClickLabel = moreOptions,
                ).semantics {
                    // The ♛ adds "Pro" to what TalkBack reads, so the name stays plain here.
                    contentDescription = entry.label
                    if (chosen) stateDescription = onScreen
                    customActions =
                        menu.map { (label, action) ->
                            CustomAccessibilityAction(label) {
                                action()
                                true
                            }
                        }
                }.padding(start = indent(depth), end = GeodeSpacing.XS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                entry.label,
                Modifier.weight(1f).padding(end = GeodeSpacing.M),
                style = MaterialTheme.typography.bodyMedium,
                color = if (chosen) accentTextColor() else cs.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            // The app's one ♛ (shared with Customize), so Pro reads the same on every page.
            if (locked) ProCrown()
            IconButton(onClick = onToggleFavourite) {
                StoneIconArt(
                    StoneIcon.FAVORITE,
                    contentDescription = favouriteAction,
                    tint = if (favourite) accentTextColor() else cs.onSurfaceVariant,
                    selected = favourite,
                )
            }
        }
        DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
            menu.forEach { (label, action) ->
                DropdownMenuItem(
                    text = { Text(label) },
                    onClick = {
                        menuOpen = false
                        action()
                    },
                )
            }
        }
    }
}

/**
 * The chosen row's matte glass: a raised white wash ([GeodeGlass.FILL_RAISED], the token for a
 * raised control on glass) in a rounded pill inset from the row's edges. Drawn behind the row, so
 * the row keeps its layout and the list does not shift when the choice moves; one rounded rect,
 * no blur or offscreen layer, so it costs nothing on a scroll over the live visual.
 */
private fun DrawScope.drawChosenGlass() {
    val inset = GeodeSpacing.S.toPx()
    val radius = GeodeRadius.Medium.toPx()
    drawRoundRect(
        color = Color.White.copy(alpha = GeodeGlass.FILL_RAISED),
        topLeft = Offset(inset, 0f),
        size = Size(size.width - 2 * inset, size.height),
        cornerRadius = CornerRadius(radius, radius),
    )
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

/**
 * The menu's open headers, kept for the process; null until the user opens or closes one, so a
 * fresh start opens the path to the style on screen instead.
 *
 * WHY not rememberSaveable alone: on a phone, rotating swaps the shell's bar layout
 * (`AppShellCompact`) for its rail layout (`AppShellExpanded`), so the whole Visuals subtree leaves
 * the composition and comes back under a different parent. `MainActivity` handles the rotation
 * itself, so nothing is restored from a bundle (device QA on #252).
 */
private object StylesGroupMemory {
    var open: Set<String>? = null
}

/** Every catalogue style as the menu shows it, in catalogue order. */
private fun styleEntries(): List<StyleEntry> =
    Catalogue.styles.map { style ->
        val family = StyleFamilies.of(style.id)
        StyleEntry(
            id = style.id,
            label = sceneDisplayLabel(style.id),
            kindLabel = kindLabel(style.kind),
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
        SceneKind.MILKDROP -> "MilkDrop"
        SceneKind.FLUID, SceneKind.CURL_FLOW, SceneKind.WATER -> "Fluid"
        SceneKind.CYMATICS -> "Cymatics"
    }

internal fun sceneDisplayLabel(id: String): String {
    val catalogued = VisualStyleCatalog.label(id)
    if (catalogued != id) return catalogued
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

private const val HALF_TURN = 180f
private const val SHADERS = "Shaders"
private val INDENT = 16.dp
private const val HUE_TURN = 360
private const val HUE_NUDGE = 40
private const val HUE_SPREAD = 50
private const val FALLBACK_SAT = 0.65f
private const val FALLBACK_DARK = 0.30f
private const val FALLBACK_LIGHT = 0.75f
