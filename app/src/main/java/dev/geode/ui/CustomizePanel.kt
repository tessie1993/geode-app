package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Casino
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SecondaryScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.data.MacroStore
import dev.geode.pro.FreeTier
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import dev.geode.ui.pro.PaywallViewModel
import kotlinx.coroutines.launch

/**
 * Visuals › Customize: seven pages by kind, stacked in one scrolling list: Macros, Look, Style,
 * Motion, Music, then Modulate and Advanced (Pro).
 *
 * WHY one list with a sticky jump header (docs/ui-redesign): a user changing "how it moves" should
 * find every moving control in one place, and should be able to scroll past it into the next kind
 * without hunting for a tab. The header names the page in view and jumps to any page; its end
 * holds the page's tools: search, Randomize, Undo and ⋯. That one 48 dp row is all the chrome
 * above the first control.
 *
 * Deeper pages ([CustomizeSubPage]) drill in: the list gives way to the page inside the same sheet,
 * with ← and its title, and back returns to the same spot in the list, because the list's scroll
 * state lives here, above both. With [nav] (the Visuals route, 1B) each deeper page is an
 * [VisualsKey.AdvancedGroup] step on the route, so system back retraces it; without it the panel
 * keeps the open page itself.
 *
 * The free tier is applied here once ([ProLock]): Macros in full, one headline control on each other
 * simple page (dev.geode.pro.FreeTier), nothing deeper. A locked slider can still be tried: it moves
 * while held and springs back on release, then the paywall offers to keep it ([LabeledSlider]).
 */
@Composable
internal fun CustomizePanel(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    nav: VisualsNavState? = null,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    var showTips by rememberSaveable { mutableStateOf(false) }
    var localSub by rememberSaveable { mutableStateOf<String?>(null) }
    val pages = CustomizeTab.PANEL.filter { it != CustomizeTab.STYLE || styleTabApplies(viz.sceneId) }
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val inView by remember(pages) {
        derivedStateOf {
            // At the end of the list the last page is in view, even when it is too short to reach the top.
            if (!listState.canScrollForward && listState.firstVisibleItemIndex > 0) pages.lastIndex else listState.firstVisibleItemIndex
        }
    }
    val page = pages[inView.coerceIn(pages.indices)]
    val unlocked = gate.allows(ProFeature.ADVANCED_CUSTOMIZATION)
    val proLock =
        remember(unlocked, viz.sceneId, paywall) {
            ProLock(
                unlocked = unlocked,
                free = FreeTier.freeCustomizeControls { key -> styleControlShown(key, viz.sceneId) },
                onLocked = { paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION)) },
            )
        }
    val host = remember(viewModel, visualsViewModel, visualizerView) { AdvancedHost(viewModel, visualsViewModel, visualizerView) }

    val subName = if (nav != null) nav.advancedGroup else localSub
    val sub = CustomizeSubPage.entries.firstOrNull { it.name == subName && it.appliesTo(viz.sceneId) }
    val open: (CustomizeSubPage) -> Unit = { target ->
        when {
            target.pro && !unlocked -> proLock.onLocked()
            nav != null -> nav.openAdvancedGroup(target.name)
            else -> localSub = target.name
        }
    }
    val close: () -> Unit = { if (nav != null) nav.pop() else localSub = null }
    // An open deeper page closes first; the route (or the shell) takes back from there.
    BackHandler(enabled = sub != null, onBack = close)

    val locked by visualsViewModel.lockedParams.collectAsStateWithLifecycle()
    CompositionLocalProvider(
        LocalParamLocks provides (locked to visualsViewModel::toggleParamLock),
        LocalProLock provides proLock,
        LocalShowTips provides showTips,
        LocalReactTo provides { open(CustomizeSubPage.REACTIONS) },
    ) {
        ParamSurface(sceneId = viz.sceneId, query = query) {
            if (sub != null) {
                val siblings = if (sub.stepped) CustomizeSubPage.of(sub.page, viz.sceneId) else emptyList()
                SubPageScreen(
                    sub = sub,
                    host = host,
                    siblings = siblings,
                    onBack = close,
                    onStep = { to ->
                        if (nav != null) nav.stepAdvanced(siblings.map { it.name }, siblings.indexOf(to) - siblings.indexOf(sub)) else localSub = to.name
                    },
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                Column(Modifier.fillMaxSize()) {
                    CustomizeHeader(
                        viewModel = viewModel,
                        visualizerView = visualizerView,
                        pages = pages,
                        page = page,
                        proLock = proLock,
                        searching = searching,
                        onSearching = { on ->
                            searching = on
                            if (!on) query = ""
                        },
                        showTips = showTips,
                        onShowTips = { showTips = it },
                        onJump = { i -> scope.launch { listState.animateScrollToItem(i) } },
                    )
                    if (searching) {
                        OutlinedTextField(
                            value = query,
                            onValueChange = { query = it },
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                            singleLine = true,
                            label = { Text("Search every control") },
                        )
                    }
                    LazyColumn(
                        state = listState,
                        modifier = Modifier.weight(1f).fillMaxWidth(),
                        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
                    ) {
                        pages.forEach { shown ->
                            item(key = shown.name, contentType = "page") {
                                CustomizePageSection(shown, viewModel, host, unlocked, onOpen = open)
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * One page in the list: its name (with ♛ when the whole page is Pro and locked), its controls,
 * then the rows that drill into its deeper pages. While searching, the deeper pages' controls are
 * drawn in place instead, so a search still reaches every control at once.
 */
@Composable
private fun CustomizePageSection(
    page: CustomizeTab,
    viewModel: PlayerViewModel,
    host: AdvancedHost,
    unlocked: Boolean,
    onOpen: (CustomizeSubPage) -> Unit,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val searching = LocalParamQuery.current.isNotBlank()
    // Macros' knobs are not searchable controls; every other page is.
    if (searching && page == CustomizeTab.MACROS) return
    val subs = CustomizeSubPage.of(page, viz.sceneId)
    Column(Modifier.fillMaxWidth()) {
        if (!searching) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(
                    page.title,
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f).semantics { heading() },
                )
                if (page in PRO_PAGES && !unlocked) ProCrown()
            }
        }
        CustomizePageBody(page, viewModel, host)
        if (searching) {
            subs.forEach { sub ->
                if (sub.pro && !unlocked) {
                    ProGated("advanced:${sub.name}", sub.title) { SubPageBody(sub, host) }
                } else {
                    SubPageBody(sub, host)
                }
            }
        } else {
            subs.forEach { sub -> SubPageRow(sub, locked = sub.pro && !unlocked) { onOpen(sub) } }
        }
    }
}

/** The pages that are Pro as a whole, so their ♛ sits on the page name. */
private val PRO_PAGES = setOf(CustomizeTab.MODULATE, CustomizeTab.ADVANCED)

/**
 * Whether [key] is a control the style page really shows on [sceneId], for picking its one free
 * control: in scope, and not one the style pins (a cymatics style with a fixed geometry).
 */
private fun styleControlShown(
    key: String,
    sceneId: String,
): Boolean =
    ParamScope.of(key).appliesTo(sceneId) &&
        when (key) {
            ParamKeys.GEOMETRY -> SceneCapabilities.isCymatics(sceneId) && Catalogue.override(sceneId, key) == null
            ParamKeys.FLUID_CURL -> SceneCapabilities.isFluid(sceneId)
            else -> true
        }

@Composable
private fun CustomizePageBody(
    page: CustomizeTab,
    viewModel: PlayerViewModel,
    host: AdvancedHost,
) {
    val visualsViewModel = host.visualsViewModel
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (SceneParams) -> Unit = visualsViewModel::editSceneParams
    when (page) {
        CustomizeTab.MACROS -> {
            val context = LocalContext.current
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            MacrosTab(macros, store)
        }
        CustomizeTab.LOOK -> {
            val artNote by viewModel.artPaletteNote.collectAsStateWithLifecycle()
            LookTab(p, onChange, onTakeArtworkPalette = viewModel::applyArtworkPalette, artworkNote = artNote)
        }
        CustomizeTab.STYLE -> StyleTab(p, onChange)
        CustomizeTab.MOTION ->
            Column {
                MovementSections(p, onChange)
                StyleMotionSections(p, onChange)
            }
        CustomizeTab.MUSIC -> {
            val reactions by visualsViewModel.reactions.collectAsStateWithLifecycle()
            EventsTab(p, onChange, reactions = reactions, onReactionsChange = visualsViewModel::setReactions)
        }
        // Modulate and Advanced are their deeper pages; the engine's roll groups are not pages.
        else -> Unit
    }
}

/** The pages Randomize and "Reset this page" have nothing to roll on: their controls are deeper pages. */
private val UNROLLED_PAGES = setOf(CustomizeTab.MODULATE, CustomizeTab.ADVANCED)

/**
 * The sticky header: the page names (the one in view lit, a tap jumps to it), then search,
 * Randomize, Undo and ⋯ (Redo, Reset this page, Reset everything, Save as preset, Show tips).
 * Randomize is the one name everywhere and rolls the page in view; it is free on Macros (the knobs'
 * Surprise) and Pro elsewhere. Icon buttons keep their full 48 dp target and carry their own names
 * for TalkBack.
 */
@Composable
private fun CustomizeHeader(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    pages: List<CustomizeTab>,
    page: CustomizeTab,
    proLock: ProLock,
    searching: Boolean,
    onSearching: (Boolean) -> Unit,
    showTips: Boolean,
    onShowTips: (Boolean) -> Unit,
    onJump: (Int) -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val history by visualsViewModel.paramHistory.collectAsStateWithLifecycle()
    var menuOpen by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    var savingPreset by remember { mutableStateOf(false) }
    val changed = remember(viz.params) { CustomizeSummary.changedCount(viz.params) }
    val context = LocalContext.current
    val macroStore = remember { MacroStore.get(context) }
    val onMacros = page == CustomizeTab.MACROS
    val rolls = page !in UNROLLED_PAGES
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
        SecondaryScrollableTabRow(
            selectedTabIndex = pages.indexOf(page).coerceAtLeast(0),
            modifier = Modifier.weight(1f),
            containerColor = Color.Transparent,
            edgePadding = 8.dp,
            divider = {},
        ) {
            pages.forEachIndexed { i, shown ->
                Tab(
                    selected = shown == page,
                    onClick = { onJump(i) },
                    text = { Text(shown.title, maxLines = 1) },
                )
            }
        }
        IconButton(onClick = { onSearching(!searching) }) {
            Icon(
                if (searching) Icons.Filled.Close else Icons.Filled.Search,
                contentDescription = if (searching) "Close search" else "Search every control",
            )
        }
        IconButton(
            enabled = rolls,
            onClick = {
                when {
                    onMacros -> macroStore.surprise()
                    !proLock.unlocked -> proLock.onLocked()
                    else -> visualsViewModel.randomizeParams(page)
                }
            },
        ) {
            Icon(Icons.Filled.Casino, contentDescription = "Randomize ${page.title}")
        }
        IconButton(onClick = visualsViewModel::undoParams, enabled = history.canUndo) {
            Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = "Undo")
        }
        Box {
            IconButton(onClick = { menuOpen = true }) {
                Icon(Icons.Filled.MoreVert, contentDescription = "More options")
            }
            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                DropdownMenuItem(
                    text = { Text("Redo") },
                    enabled = history.canRedo,
                    leadingIcon = { Icon(Icons.AutoMirrored.Filled.Redo, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        visualsViewModel.redoParams()
                    },
                )
                DropdownMenuItem(
                    text = { Text("Reset ${page.title}") },
                    enabled = rolls,
                    onClick = {
                        menuOpen = false
                        if (onMacros) macroStore.resetKnobs() else visualsViewModel.resetCustomizeTab(page)
                    },
                )
                DropdownMenuItem(
                    text = { Text(if (changed == 0) "Reset everything" else "Reset everything ($changed changed)") },
                    enabled = changed > 0,
                    onClick = {
                        menuOpen = false
                        confirmReset = true
                    },
                )
                DropdownMenuItem(
                    text = { Text("Save as preset") },
                    onClick = {
                        menuOpen = false
                        savingPreset = true
                    },
                )
                DropdownMenuItem(
                    text = { Text("Show tips") },
                    trailingIcon = { Checkbox(checked = showTips, onCheckedChange = null) },
                    onClick = {
                        menuOpen = false
                        onShowTips(!showTips)
                    },
                )
            }
        }
    }
    if (savingPreset) {
        SavePresetDialog(
            onDismiss = { savingPreset = false },
            onSave = { name ->
                // The one save path: the same call the Presets page makes.
                visualsViewModel.savePreset(name, visualizerView.visualizerRenderer.customShaderFor(viewModel.vizState.value.sceneId))
                savingPreset = false
            },
        )
    }
    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Reset everything?") },
            text = {
                Text(
                    "Puts all $changed changed controls back to their defaults, on every page. " +
                        "Undo brings them back, and saved presets are not touched.",
                )
            },
            confirmButton = {
                CrystalButton(onClick = {
                    visualsViewModel.resetAllCustomize()
                    confirmReset = false
                }) { Text("Reset") }
            },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun SavePresetDialog(
    onDismiss: () -> Unit,
    onSave: (String) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Save as preset") },
        text = {
            OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true, placeholder = { Text("Preset name") })
        },
        confirmButton = { CrystalButton(enabled = name.isNotBlank(), onClick = { onSave(name.trim()) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
