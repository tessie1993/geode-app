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
import androidx.compose.material3.TextButton
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
import dev.geode.engine.bridge.CustomizeNative
import dev.geode.engine.bridge.SoundNative
import dev.geode.geodeContainer
import dev.geode.model.StyleCatalogue
import dev.geode.model.StyleEntry
import dev.geode.model.VizPlaylistEntry
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
 * The families are one flat level, in the native catalogue's order ([StyleCatalogue.picker]): Silk,
 * Life, Mycelium, Morphing, Shaders (every shader style outside a shader family), each shader
 * family (Arbor, Bulb…), MilkDrop, Fluid, Cymatics. Which style is in which family is native.
 *
 * "Your styles" (on screen, favourites, recent) is the dropdown's first entry rather than a section
 * above the list, so the page stays one control over one list. The style on screen leads every list
 * (owner, 2026-11-12): first in Your styles, its family first after Your styles in the dropdown, and
 * first in its family's list, the rest in the catalogue's order (native `core/viz/StylePick`).
 *
 * The style on screen is named in a header above the list (native `styleLabel`), which stays put while the
 * list scrolls and changes at once on a tap or a random pick; its row in the list is lit too. Rows keep their
 * catalogue order, so a tapped style stays where it was tapped (owner, 2026-11-18: it used to jump to the top
 * and vanish from under the finger); the dropdown still leads with the family on screen. Opening a family
 * scrolls to the style on screen when it is in that list.
 *
 * Two random buttons sit under the dropdown (owner, 2026-11-18). "Random style" draws from every style of every
 * family, never the one on screen. "Random family" draws a family (not the one on screen's, each equally likely
 * whatever its size), then a style in it, and opens that family in the dropdown so the new style shows in
 * its list. Both give a free user a style they can keep while there is one. The draws are native
 * (`core/viz/StylePick`).
 *
 * A tap loads the style: the visual changes at once, and that is the choice (no "Use style" step).
 * A Pro style a free user taps still plays on the visual while the paywall offers it, as a half
 * sheet under the visual (`StylePaywallSheet`) so the preview stays in view. Closing the paywall
 * without buying, or leaving the page, puts the chosen style back ([PlayerViewModel.leaveScenePreview]).
 * The heart is the one-tap favourite; a long press opens the row's menu (favourite, add to the
 * visuals playlist, remove from Recent), and TalkBack gets the same items as custom actions. The
 * swipe on the visual steps through the styles in the catalogue's order.
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
    val families = remember { StyleCatalogue.picker }
    val familyNames = remember { families.keys.toList() }
    val order = remember { StyleCatalogue.ids }
    // Each style's family index, for the native draw's "Family only".
    val familyOf = remember { IntArray(order.size) { i -> familyNames.indexOfFirst { order[i] in families.getValue(it) } } }
    val labels = remember { order.associateWith { CustomizeNative.styleLabel(it) } }
    var picked by remember {
        mutableStateOf(
            StylesFamilyMemory.picked
                ?: families.entries.firstOrNull { viz.sceneId in it.value }?.key
                ?: YOURS,
        )
    }
    val mine = shelf.yours(order, null)
    val onScreenFamily = familyOf.getOrElse(order.indexOf(viz.sceneId)) { -1 }
    val familyOptions = remember(onScreenFamily) { SoundNative.stylesLead(familyNames.size, onScreenFamily).map(familyNames::get) }
    val members = families[picked].orEmpty()
    val ids = if (picked == YOURS) mine else members
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
    val open = BooleanArray(order.size) { gate.allowsStyle(order[it]) }
    val onScreenAt = order.indexOf(viz.sceneId)
    val shuffleStyle: () -> Unit = {
        val next = SoundNative.stylesRandom(familyOf, open, onScreenAt, false, 0, -1)
        if (next >= 0) {
            view.performStoneHaptic(StoneHapticCue.CONFIRM)
            load(order[next])
        }
    }
    val shuffleFamily: () -> Unit = {
        val next = SoundNative.stylesRandomFamily(familyOf, open, onScreenAt)
        if (next >= 0) {
            view.performStoneHaptic(StoneHapticCue.CONFIRM)
            picked = familyNames[familyOf[next]]
            StylesFamilyMemory.picked = picked
            load(order[next])
        }
    }
    val list = rememberLazyListState()
    // A new pick opens on the style on screen when it is in the list, else at the top. Only a new pick scrolls:
    // a tap leaves the list where it is.
    LaunchedEffect(picked) { list.scrollToItem(maxOf(0, ids.indexOf(viz.sceneId))) }
    val yoursLabel = stringResource(R.string.styles_yours)
    val familyLabel: (String) -> String = { if (it == YOURS) yoursLabel else CustomizeNative.styleFamilyLabel(it) }
    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(start = GeodeSpacing.S, end = GeodeSpacing.XXS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            LibraryDropdownRow(
                value = familyLabel(picked),
                options = listOf(YOURS) + familyOptions,
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
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.XS)) {
            TextButton(onClick = shuffleStyle) {
                StoneIconArt(StoneIcon.SHUFFLE, contentDescription = null, tint = accentTextColor())
                Text(stringResource(R.string.styles_random), Modifier.padding(start = GeodeSpacing.XS), color = accentTextColor())
            }
            TextButton(onClick = shuffleFamily) {
                StoneIconArt(StoneIcon.SHUFFLE, contentDescription = null, tint = accentTextColor())
                Text(stringResource(R.string.styles_random_family), Modifier.padding(start = GeodeSpacing.XS), color = accentTextColor())
            }
        }
        NowPlaying(labels[viz.sceneId] ?: sceneDisplayLabel(viz.sceneId))
        LazyColumn(
            state = list,
            contentPadding = PaddingValues(bottom = GeodeSpacing.S),
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

/** The style on screen, named above the list and outside it, so it stays in view while the list scrolls. */
@Composable
private fun NowPlaying(label: String) {
    val caption = stringResource(R.string.styles_now_playing)
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XXS)
            .semantics(mergeDescendants = true) {
                contentDescription = "$caption: $label"
            },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(caption, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(
            label,
            Modifier.weight(1f).padding(start = GeodeSpacing.S),
            style = MaterialTheme.typography.titleSmall,
            color = accentTextColor(),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun StylesHint(text: String) {
    Text(
        text,
        Modifier.padding(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
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
                }.padding(start = GeodeSpacing.M, end = GeodeSpacing.XXS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                entry.label,
                Modifier.weight(1f).padding(end = GeodeSpacing.S),
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

/** A style's name as people read it (core/viz/StyleNames). */
internal fun sceneDisplayLabel(id: String): String = CustomizeNative.styleLabel(id)

/** The dropdown's own entry for the user's shelf; never a catalogue family name (those are lowercase words). */
private const val YOURS = "#yours"
