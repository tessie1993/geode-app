package dev.geode.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.geodeContainer
import dev.geode.model.StyleEntry
import dev.geode.model.VizPlaylistEntry
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.performStoneHaptic

/**
 * Visuals › Styles: one dropdown that picks a family, and under it that family's styles as a plain
 * list (owner, 2026-11-11: "no nested families in families"; it was family › style › variation).
 *
 * The families are one flat level, in the native catalogue's order ([Catalogue.picker]): Silk,
 * Life, Mycelium, Morphing, Shaders (every shader style outside a shader family), each shader
 * family (Arbor, Bulb…), MilkDrop, Fluid, Cymatics. Which style is in which family is native.
 *
 * "Your styles" (on screen, favourites, recent) is the dropdown's first entry rather than a section
 * above the list, so the page stays one control over one list; Random is the icon beside the
 * dropdown and loads a style the user can keep while there is one, never the one on screen. The
 * dropdown opens on the family of the style on screen and keeps the last pick for the process.
 *
 * A tap loads the style: the visual changes at once, and that is the choice (no "Use style" step).
 * A Pro style a free user taps still plays on the visual while the paywall offers it, as a half
 * sheet under the visual (`StylePaywallSheet`) so the preview stays in view. Closing the paywall
 * without buying, or leaving the page, puts the chosen style back ([PlayerViewModel.leaveScenePreview]).
 * The heart is the one-tap favourite; a long press opens the row's menu (favourite, add to the
 * visuals playlist, remove from Recent), and TalkBack gets the same items as custom actions. The
 * swipe on the visual steps through the styles in this order.
 */
@Composable
internal fun StylesTab(viewModel: PlayerViewModel) {
    val context = LocalContext.current
    val shelfStore = remember { context.geodeContainer.styleShelf }
    val shelf by shelfStore.shelf.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val paywall: PaywallViewModel = geodeViewModel()
    val paywallState by paywall.uiState.collectAsStateWithLifecycle()
    val paywallOpen = paywallState.visible
    val families = remember { Catalogue.picker }
    val order = remember { Catalogue.styles.map { it.id } }
    val labels = remember { order.associateWith(::sceneDisplayLabel) }
    var picked by remember {
        mutableStateOf(
            StylesFamilyMemory.picked
                ?: families.entries.firstOrNull { viz.sceneId in it.value }?.key
                ?: YOURS,
        )
    }
    val mine = shelf.yours(order, viz.sceneId)
    val ids = if (picked == YOURS) mine else families[picked].orEmpty()
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
        val others = order.filter { it != viz.sceneId }
        // A free user lands on a style they can keep while there is one.
        others.filter { gate.allowsStyle(it) }.ifEmpty { others }.randomOrNull()?.let {
            view.performStoneHaptic(StoneHapticCue.CONFIRM)
            load(it)
        }
    }
    val list = rememberLazyListState()
    // A new pick starts at its top, or at the style on screen when it is there.
    LaunchedEffect(picked) { list.scrollToItem(ids.indexOf(viz.sceneId).coerceAtLeast(0)) }
    val yoursLabel = stringResource(R.string.styles_yours)
    val familyLabel: (String) -> String = { if (it == YOURS) yoursLabel else VisualStyleCatalog.familyLabel(it) }
    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(start = GeodeSpacing.M, end = GeodeSpacing.XS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            LibraryDropdownRow(
                value = familyLabel(picked),
                options = listOf(YOURS) + families.keys,
                label = { familyLabel(it) },
                isSelected = { it == picked },
                onPick = {
                    picked = it
                    StylesFamilyMemory.picked = it
                },
                modifier = Modifier.weight(1f),
                leading = {
                    StoneIconArt(
                        StoneIcon.VISUALIZER,
                        contentDescription = stringResource(R.string.styles_family),
                        tint = accentTextColor(),
                    )
                },
                detail = { (if (it == YOURS) mine else families[it].orEmpty()).size.toString() },
            )
            IconButton(onClick = shuffle) {
                StoneIconArt(StoneIcon.SHUFFLE, contentDescription = stringResource(R.string.styles_random), tint = accentTextColor())
            }
        }
        LazyColumn(
            state = list,
            contentPadding = PaddingValues(bottom = GeodeSpacing.L),
            modifier = Modifier.fillMaxWidth().weight(1f),
        ) {
            if (ids.isEmpty()) {
                item(key = "empty", contentType = "hint") { StylesHint(stringResource(R.string.styles_yours_empty)) }
            }
            items(ids, key = { it }, contentType = { "style" }) { id ->
                StyleRow(
                    entry = StyleEntry(id, labels[id] ?: sceneDisplayLabel(id)),
                    chosen = id == viz.sceneId,
                    locked = !gate.allowsStyle(id),
                    favourite = shelf.isFavourite(id),
                    recent = id in shelf.recents,
                    onTap = { load(id) },
                    onToggleFavourite = { shelfStore.toggleFavourite(id) },
                    onAddToPlaylist = { viewModel.addToVizPlaylist(VizPlaylistEntry(sceneId = id, label = labels[id] ?: id)) },
                    onRemoveRecent = { shelfStore.removeRecent(id) },
                )
            }
        }
    }
}

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
 * One style: its name (lit when it is on screen), a ♛ when it is Pro, and a heart. The row is one 48 dp target; the heart is its own 48 dp button, the one-tap favourite.
 *
 * A long press opens the row's menu, as on any Android list row: favourite, add to the visuals
 * playlist (not for a locked style, which the playlist would only skip), and remove from Recent
 * when it is there. TalkBack reads the same items as custom actions, so nothing hides behind a hold.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun StyleRow(
    entry: StyleEntry,
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
                }.padding(start = GeodeSpacing.L, end = GeodeSpacing.XS),
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
 * The family last picked, kept for the process; null until the user picks one, so a fresh start
 * opens on the family of the style on screen.
 *
 * WHY not rememberSaveable alone: on a phone, rotating swaps the shell's bar layout
 * (`AppShellCompact`) for its rail layout (`AppShellExpanded`), so the whole Visuals subtree leaves
 * the composition and comes back under a different parent. `MainActivity` handles the rotation
 * itself, so nothing is restored from a bundle (device QA on #252).
 */
private object StylesFamilyMemory {
    var picked: String? = null
}

internal fun sceneDisplayLabel(id: String): String {
    val catalogued = VisualStyleCatalog.label(id)
    if (catalogued != id) return catalogued
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

/** The dropdown's own entry for the user's shelf; never a catalogue family name (those are lowercase words). */
private const val YOURS = "#yours"
