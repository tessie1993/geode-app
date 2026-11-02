package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Casino
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ExpandMore
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
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.AutoDirectorStore
import dev.geode.data.MacroStore
import dev.geode.pro.FreeTier
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.HandReactions
import dev.geode.render.VisualizerView
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.ParamFormat
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneParams
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.selectedGlow
import kotlinx.coroutines.launch
import kotlin.math.abs

/**
 * Visuals › Customize: an overview of seven cards by kind, in editing order: Macros, Look, Style,
 * Motion, Music, then Modulate and Advanced (Pro).
 *
 * WHY an accordion (UI V2 rule 1, docs/ui-redesign; Lightroom's solo panels): the panel opens as a
 * table of contents, every card collapsed, each saying its state in one line on the right ("Neon",
 * "2 mine", "As styled"), so the user sees the whole look before touching it. A tap opens a card in
 * place and closes the one that was open, so the list is never longer than one page of controls,
 * and scrolls the opened card to the top. There is no second tab row: the cards are the pages. The
 * one 48 dp row above them holds the state of the whole look and the tools: search, Randomize, Undo
 * and ⋯. Randomize and "Reset" act on the open card, or on Macros while every card is closed.
 *
 * Deeper pages ([CustomizeSubPage]) still drill in: the cards give way to the page inside the same
 * sheet, with ← and its title, and back returns to the same card, because the list's scroll state
 * and the open card live here, above both. With [nav] (the Visuals route) the open card is a
 * [VisualsKey.CustomizeCard] and each deeper page an [VisualsKey.AdvancedGroup] on top of it, so
 * system back retraces them (page, then card, then the tab before) and the true history of UI V2
 * holds across cards; without it the panel keeps the open card and page itself.
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
    // Collapsed by default; one card open at a time, kept by name across rotation. With [nav] the open card is a step
    // on the Visuals route, so system back collapses it and a return from another tab finds it open.
    var localCard by rememberSaveable { mutableStateOf<String?>(null) }
    val openCard = if (nav != null) nav.openCard else localCard
    val setCard: (String?) -> Unit = { name ->
        when {
            nav == null -> localCard = name
            name != null -> nav.openCustomizeCard(name)
            else -> nav.closeCustomizeCard()
        }
    }
    val pages = CustomizeTab.PANEL.filter { it != CustomizeTab.STYLE || styleTabApplies(viz.sceneId) }
    val expanded = pages.firstOrNull { it.name == openCard }
    // A saved card the panel no longer shows (the style changed) would be a back step with nothing to undo.
    LaunchedEffect(openCard, expanded) { if (openCard != null && expanded == null) setCard(null) }
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
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
                        if (nav != null) {
                            nav.stepAdvanced(siblings.map { it.name }, siblings.indexOf(to) - siblings.indexOf(sub))
                        } else {
                            localSub = to.name
                        }
                    },
                    modifier = Modifier.fillMaxSize(),
                )
            } else {
                Column(Modifier.fillMaxSize()) {
                    CustomizeHeader(
                        viewModel = viewModel,
                        visualizerView = visualizerView,
                        page = expanded ?: CustomizeTab.MACROS,
                        proLock = proLock,
                        searching = searching,
                        onSearching = { on ->
                            searching = on
                            if (!on) query = ""
                        },
                        showTips = showTips,
                        onShowTips = { showTips = it },
                    )
                    if (searching) {
                        OutlinedTextField(
                            value = query,
                            onValueChange = { query = it },
                            modifier = Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L),
                            singleLine = true,
                            label = { Text("Search every control") },
                        )
                    }
                    LazyColumn(
                        state = listState,
                        modifier = Modifier.weight(1f).fillMaxWidth(),
                        contentPadding =
                            PaddingValues(start = GeodeSpacing.M, end = GeodeSpacing.M, top = GeodeSpacing.XS, bottom = GeodeSpacing.XL),
                        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
                    ) {
                        if (query.isNotBlank()) {
                            // A search shows every match at once, deeper pages included, without the cards.
                            itemsIndexed(pages, key = { _, shown -> shown.name }, contentType = { _, _ -> "results" }) { _, shown ->
                                CustomizeSearchResults(shown, viewModel, host, unlocked)
                            }
                        } else {
                            itemsIndexed(pages, key = { _, shown -> shown.name }, contentType = { _, _ -> "card" }) { i, shown ->
                                CustomizePageCard(
                                    page = shown,
                                    viewModel = viewModel,
                                    host = host,
                                    unlocked = unlocked,
                                    expanded = shown == expanded,
                                    onToggle = {
                                        val opening = shown != expanded
                                        setCard(if (opening) shown.name else null)
                                        if (opening) scope.launch { listState.animateScrollToItem(i) }
                                    },
                                    onOpen = open,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/** How far a card lifts off the sheet: a soft shadow, a fraction of the sheet's own, so seven stay cheap. */
private val CARD_ELEVATION = 4.dp
private const val HALF_TURN = 180f

/**
 * One page as a collapsible card: its name (with ♛ when the whole page is Pro and locked), the
 * page's state in one line on the right, a ▾ that turns over when it opens, and its ⋮. Open, its
 * controls follow, then the rows that drill into its deeper pages. It grows and shrinks from its
 * header with the theme's spatial spring (none under reduced motion).
 */
@Composable
private fun CustomizePageCard(
    page: CustomizeTab,
    viewModel: PlayerViewModel,
    host: AdvancedHost,
    unlocked: Boolean,
    expanded: Boolean,
    onToggle: () -> Unit,
    onOpen: (CustomizeSubPage) -> Unit,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val subs = CustomizeSubPage.of(page, viz.sceneId)
    val motion = LocalGeodeMotion.current
    val turn by animateFloatAsState(if (expanded) HALF_TURN else 0f, motion.fastSpatialSpec(), label = "card chevron")
    val shape = RoundedCornerShape(GeodeRadius.Large)
    val stateText = stringResource(if (expanded) R.string.customize_card_expanded else R.string.customize_card_collapsed)
    val actionText = stringResource(if (expanded) R.string.customize_card_collapse else R.string.customize_card_expand)
    Column(
        Modifier
            .fillMaxWidth()
            .selectedGlow(expanded, shape)
            // The default matte fill, not the thinnest: the card sits on the sheet's own glass, and its summary
            // text must stay readable over a bright frame (WCAG 1.4.3).
            .floatingGlass(shape = shape, elevation = CARD_ELEVATION)
            .animateContentSize(motion.defaultSpatialSpec()),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = CARD_HEADER_HEIGHT)
                .clickable(role = Role.Button, onClickLabel = actionText, onClick = onToggle)
                .semantics {
                    heading()
                    stateDescription = stateText
                }.padding(start = GeodeSpacing.L, end = GeodeSpacing.XS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(page.title, style = MaterialTheme.typography.titleSmall)
            if (page in PRO_PAGES && !unlocked) ProCrown()
            Text(
                pageSummary(page, viz.sceneId, viz.params, host, subs),
                modifier = Modifier.weight(1f).padding(start = GeodeSpacing.S),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.End,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(
                Icons.Filled.ExpandMore,
                contentDescription = null,
                modifier = Modifier.padding(start = GeodeSpacing.XS).rotate(turn),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            PageMenu(page, host, unlocked)
        }
        if (expanded) {
            Column(Modifier.fillMaxWidth().padding(start = GeodeSpacing.L, end = GeodeSpacing.L, bottom = GeodeSpacing.M)) {
                CustomizePageBody(page, viewModel, host)
                subs.forEach { sub -> SubPageRow(sub, locked = sub.pro && !unlocked) { onOpen(sub) } }
            }
        }
    }
}

private val CARD_HEADER_HEIGHT = 56.dp

/** While searching: a page's matching controls, its deeper pages' too, drawn in place. */
@Composable
private fun CustomizeSearchResults(
    page: CustomizeTab,
    viewModel: PlayerViewModel,
    host: AdvancedHost,
    unlocked: Boolean,
) {
    // Macros' knobs are not searchable controls; every other page is.
    if (page == CustomizeTab.MACROS) return
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    Column(Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.XS)) {
        CustomizePageBody(page, viewModel, host)
        CustomizeSubPage.of(page, viz.sceneId).forEach { sub ->
            if (sub.pro && !unlocked) {
                ProGated("advanced:${sub.name}", sub.title) { SubPageBody(sub, host) }
            } else {
                SubPageBody(sub, host)
            }
        }
    }
}

/**
 * A card's state in one line: real values rather than descriptions ("Neon", "2 turned", "Speed ×1.5"),
 * so the closed overview says how the look stands.
 */
@Composable
private fun pageSummary(
    page: CustomizeTab,
    sceneId: String,
    p: SceneParams,
    host: AdvancedHost,
    subs: List<CustomizeSubPage>,
): String =
    when (page) {
        CustomizeTab.MACROS -> {
            val context = LocalContext.current
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            val turned = macros.knobs.count { abs(it) >= KNOB_EPSILON }
            if (turned == 0) {
                stringResource(R.string.customize_summary_as_styled)
            } else {
                pluralStringResource(R.plurals.macros_summary_turned, turned, turned)
            }
        }
        CustomizeTab.LOOK -> {
            val saved = rememberSavedPalettes().items
            val labels = SceneParams.PALETTES.map { it.first } + saved.map { it.name }
            labels.getOrNull(paletteChipIndex(p, saved, second = false)) ?: stringResource(R.string.look_summary_custom)
        }
        CustomizeTab.STYLE -> sceneDisplayLabel(sceneId)
        CustomizeTab.MOTION -> {
            val shown = ParamFormat.format(p.speed, ParamFormat.unitFor(ParamKeys.SPEED, SPEED_RANGE), SceneParams.DEFAULT.speed)
            stringResource(R.string.motion_summary, shown)
        }
        CustomizeTab.MUSIC -> {
            val reactions by host.visualsViewModel.reactions.collectAsStateWithLifecycle()
            val handMask by host.visualsViewModel.handMask.collectAsStateWithLifecycle()
            val hand = HandReactions(reactions, handMask)
            val mine = remember(sceneId, p.reactSeed, hand) { MusicMoments.rows(sceneId, p.reactSeed, hand).count { it.mine } }
            if (mine == 0) {
                stringResource(R.string.music_summary_auto)
            } else {
                pluralStringResource(R.plurals.music_summary_mine, mine, mine)
            }
        }
        CustomizeTab.MODULATE -> {
            val reactions by host.visualsViewModel.reactions.collectAsStateWithLifecycle()
            stringResource(if (reactions == null) R.string.modulate_summary_style else R.string.modulate_summary_yours)
        }
        else -> subs.joinToString { it.title }
    }

/** Motion › Speed's range, as MovementSections slides it, for the card's summary of it. */
private val SPEED_RANGE = 0.05f..4f
private const val KNOB_EPSILON = 0.005f

/** What a card's ⋮ › What is this says about the page. */
private val PAGE_ABOUT =
    mapOf(
        CustomizeTab.MACROS to R.string.customize_about_macros,
        CustomizeTab.LOOK to R.string.customize_about_look,
        CustomizeTab.STYLE to R.string.customize_about_style,
        CustomizeTab.MOTION to R.string.customize_about_motion,
        CustomizeTab.MUSIC to R.string.customize_about_music,
        CustomizeTab.MODULATE to R.string.customize_about_modulate,
        CustomizeTab.ADVANCED to R.string.customize_about_advanced,
    )

/**
 * A card's ⋮: What is this, then Randomize and Reset for the pages that own controls. Randomize is
 * free on Macros (the knobs' Surprise) and Pro elsewhere, as in the header.
 */
@Composable
private fun PageMenu(
    page: CustomizeTab,
    host: AdvancedHost,
    unlocked: Boolean,
) {
    val context = LocalContext.current
    val macroStore = remember { MacroStore.get(context) }
    val lock = LocalProLock.current
    var open by remember { mutableStateOf(false) }
    var explaining by remember { mutableStateOf(false) }
    val rolls = page !in UNROLLED_PAGES
    Box {
        IconButton(onClick = { open = true }) {
            Icon(Icons.Filled.MoreVert, contentDescription = "More for ${page.title}", tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            DropdownMenuItem(text = { Text(stringResource(R.string.customize_what_is_this)) }, onClick = {
                open = false
                explaining = true
            })
            if (rolls) {
                DropdownMenuItem(
                    text = { Text("Randomize ${page.title}") },
                    leadingIcon = { Icon(Icons.Filled.Casino, contentDescription = null) },
                    onClick = {
                        open = false
                        when {
                            page == CustomizeTab.MACROS -> macroStore.surprise()
                            !unlocked -> lock.onLocked()
                            else -> host.visualsViewModel.randomizeParams(page)
                        }
                    },
                )
                DropdownMenuItem(text = { Text("Reset ${page.title}") }, onClick = {
                    open = false
                    if (page == CustomizeTab.MACROS) macroStore.resetKnobs() else host.visualsViewModel.resetCustomizeTab(page)
                })
            }
        }
    }
    val about = PAGE_ABOUT[page]
    if (explaining && about != null) AboutDialog(page.title, stringResource(about)) { explaining = false }
}

/** The pages that are Pro as a whole, so their ♛ sits on the card's name. */
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
            val handMask by visualsViewModel.handMask.collectAsStateWithLifecycle()
            Column {
                DirectorSwitch()
                EventsTab(
                    p,
                    onChange,
                    hand = HandReactions(reactions, handMask),
                    onReactionsChange = visualsViewModel::setReactions,
                    onHandChange = visualsViewModel::setHandReactions,
                )
            }
        }
        // Modulate and Advanced are their deeper pages; the engine's roll groups are not pages.
        else -> Unit
    }
}

/**
 * Music's first row: the auto-director, the same switch (and store) as Settings › Behaviour ›
 * Auto-director and Visuals' quick settings, so the three never disagree. What the free tier gets
 * from it (the first minute of each song) is the store's policy, not this row's.
 */
@Composable
private fun DirectorSwitch() {
    val label = stringResource(R.string.customize_director)
    if (!visible(label, ParamScope.UNIVERSAL)) return
    val context = LocalContext.current
    val store = remember { AutoDirectorStore.get(context) }
    val on by store.enabled.collectAsStateWithLifecycle()
    LabeledSwitch(label, checked = on, onCheckedChange = store::setEnabled)
}

/** The pages Randomize and "Reset this page" have nothing to roll on: their controls are deeper pages. */
private val UNROLLED_PAGES = setOf(CustomizeTab.MODULATE, CustomizeTab.ADVANCED)

/**
 * The one row above the cards: how far the look is from as styled, then search, Randomize, Undo and
 * ⋯ (Redo, Reset the open card, Reset everything, Save as preset, Show tips). Randomize rolls the
 * open card ([page]: Macros while every card is closed); it is free on Macros (the knobs' Surprise)
 * and Pro elsewhere. Icon buttons keep their full 48 dp target and carry their own names for TalkBack.
 */
@Composable
private fun CustomizeHeader(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    page: CustomizeTab,
    proLock: ProLock,
    searching: Boolean,
    onSearching: (Boolean) -> Unit,
    showTips: Boolean,
    onShowTips: (Boolean) -> Unit,
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
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Text(
            if (changed == 0) {
                stringResource(R.string.customize_summary_as_styled)
            } else {
                pluralStringResource(R.plurals.customize_summary_changed, changed, changed)
            },
            modifier = Modifier.weight(1f).padding(start = GeodeSpacing.L),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
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
        val sounds = LocalUiSounds.current
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Reset everything?") },
            text = {
                Text(
                    "Puts all $changed changed controls back to their defaults, on every page. " +
                        "Undo brings them back, and saved presets are not touched.",
                )
            },
            // Dialog actions are text buttons (M3); the confirm keeps the old button's confirm sound.
            confirmButton = {
                TextButton(onClick = {
                    sounds.play(UiSound.CONFIRM)
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
    val sounds = LocalUiSounds.current
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Save as preset") },
        text = {
            OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true, placeholder = { Text("Preset name") })
        },
        confirmButton = {
            TextButton(enabled = name.isNotBlank(), onClick = {
                sounds.play(UiSound.CONFIRM)
                onSave(name.trim())
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
