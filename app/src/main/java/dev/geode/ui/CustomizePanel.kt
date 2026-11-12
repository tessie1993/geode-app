package dev.geode.ui

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
import androidx.compose.foundation.lazy.items
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
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.model.GestureTip
import dev.geode.model.TourSpot
import dev.geode.pro.FreeTier
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.SceneParams
import dev.geode.stage.StageView
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.UiSound
import dev.geode.ui.tour.LocalTourNudge
import dev.geode.ui.tour.tourTarget
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map

/**
 * Visuals › Customize: eleven categories by what their controls do (issue #333), drawn by
 * [CustomizeBrowser].
 *
 * Everything about the controls is native (core/viz/CustomizeMap): which category and section
 * each sits in, which sections are Basics (open at the top, free) and which are Pro, which
 * categories and sections the style on screen has (Camera only on 3D styles,
 * Engine on Fluid, Cymatics and MilkDrop), and what a category's Randomize and Reset do.
 * This file wires that to the screen: the free tier through [ProLock] (a Pro control can be
 * tried, then springs back and the paywall offers it), the tab on screen and the groups open in
 * each (`CustomizeGroupsStore`, kept across restarts: a tab is no back step, so back leaves
 * Customize), and the one row of tools above it all.
 */
@Composable
internal fun CustomizePanel(
    viewModel: PlayerViewModel,
    visualizerView: StageView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val vizState = viewModel.vizState.collectAsStateWithLifecycle()
    val sceneId by remember { derivedStateOf { vizState.value.sceneId } }
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    var showTips by rememberSaveable { mutableStateOf(false) }
    val unlocked = gate.allows(ProFeature.ADVANCED_CUSTOMIZATION)
    val proLock =
        remember(unlocked, paywall) {
            ProLock(
                unlocked = unlocked,
                free = FreeTier.freeCustomizeControls(CustomizeMapTable.basicControls),
                onLocked = { paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION)) },
            )
        }
    val host = remember(viewModel, visualsViewModel, visualizerView) { AdvancedHost(viewModel, visualsViewModel, visualizerView) }

    // What this style offers, as native says.
    val shown = remember(sceneId) { CustomizeMapTable.shownOn(sceneId) }
    val categories = remember(shown) { CustomizeCategory.entries.filter { it.name in shown } }
    // Built once per style, not on each pass (a slider drag recomposes this panel), and one stable lambda so the browser can skip.
    val sectionsByCategory =
        remember(shown) {
            CustomizeCategory.entries.associateWith { c -> CustomizeMapTable.sectionsOf(c).filter { it.path in shown } }
        }
    val sectionsOf: (CustomizeCategory) -> List<CustomizeSection> = remember(sectionsByCategory) { { c -> sectionsByCategory.getValue(c) } }

    val context = LocalContext.current
    val groups = remember { context.geodeContainer.customizeGroups }
    val remembered by groups.groups.collectAsStateWithLifecycle()
    val openGroups = remembered.open
    // A saved tab this style lacks (Engine after leaving Fluid) shows the first one, and comes back with the style.
    val open = categories.firstOrNull { it.name == remembered.tab } ?: categories.firstOrNull() ?: CustomizeCategory.QUICK_KNOBS
    val locked by visualsViewModel.lockedParams.collectAsStateWithLifecycle()

    CompositionLocalProvider(
        LocalParamLocks provides (locked to visualsViewModel::toggleParamLock),
        LocalProLock provides proLock,
        LocalShowTips provides showTips,
        LocalReactTo provides {
            // "Make it react to music": Music, with its Reactions open.
            groups.expand("${CustomizeCategory.MUSIC.name}/reactions")
            groups.selectTab(CustomizeCategory.MUSIC.name)
        },
    ) {
        ProvideKnobLinks(sceneId, proLock) {
            ParamSurface(sceneId = sceneId, query = query) {
                val header: @Composable (CustomizeCategory) -> Unit = { category ->
                    CustomizeHeader(
                        viewModel = viewModel,
                        visualizerView = visualizerView,
                        category = category,
                        canRoll = category == CustomizeCategory.QUICK_KNOBS || "${category.name}:roll" in shown,
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
                            modifier = Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.S),
                            singleLine = true,
                            label = { Text(stringResource(R.string.customize_a11y_search)) },
                        )
                    }
                }
                if (query.isNotBlank()) {
                    // A search shows every match at once, in every category; each control hides itself unless it matches.
                    Column(Modifier.fillMaxSize()) {
                        header(open)
                        LazyColumn(
                            modifier = Modifier.weight(1f).fillMaxWidth(),
                            contentPadding = PaddingValues(horizontal = GeodeSpacing.S, vertical = GeodeSpacing.XS),
                            verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
                        ) {
                            items(categories.flatMap(sectionsOf), key = { it.path }) { section ->
                                Column(Modifier.fillMaxWidth()) { CustomizeControls(section.controls, host) }
                            }
                        }
                    }
                } else {
                    CustomizeBrowser(
                        categories = categories,
                        selected = open,
                        onSelect = { groups.selectTab(it.name) },
                        sectionsOf = sectionsOf,
                        openGroups = openGroups,
                        onToggleGroup = { groups.toggle(it.path) },
                        // This tab only; groups.collapseAll(null) would close every tab's.
                        onCollapseAll = { groups.collapseAll(it.name) },
                        sectionLocked = { !proLock.unlocked && !FreeTier.customizeSectionFree(it.basics) },
                        modifier = Modifier.fillMaxSize(),
                        header = header,
                    ) { section -> CustomizeControls(section.controls, host) }
                }
            }
        }
    }
}

private fun changedCount(params: SceneParams): Int = GeodeNative.customizeChangedCount(SceneParamsCodec.pack(params))

/**
 * The one row above the tabs: how far the look is from as styled, then search, Randomize, Undo
 * and ⋯ (Redo, Randomize every group, Reset this category, Keep it, Take it from a preset, Reset
 * everything, Save as preset, Show tips). Randomize rolls the tab on screen ([category]); without
 * Pro it rolls the free Basics, and offers Pro where a category has none to roll. Quick knobs'
 * Randomize is free: new turns for the knobs on screen. Native decides what each one moves and
 * whether a category has a roll ([canRoll]).
 */
@Composable
private fun CustomizeHeader(
    viewModel: PlayerViewModel,
    visualizerView: StageView,
    category: CustomizeCategory,
    canRoll: Boolean,
    proLock: ProLock,
    searching: Boolean,
    onSearching: (Boolean) -> Unit,
    showTips: Boolean,
    onShowTips: (Boolean) -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val changed by remember(viewModel) {
        viewModel.vizState.map { changedCount(it.params) }.distinctUntilChanged()
    }.collectAsStateWithLifecycle(remember(viewModel) { changedCount(viewModel.vizState.value.params) })
    val history by visualsViewModel.paramHistory.collectAsStateWithLifecycle()
    var menuOpen by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    var savingPreset by remember { mutableStateOf(false) }
    var borrowing by remember { mutableStateOf(false) }
    val context = LocalContext.current
    val macroStore = remember { context.geodeContainer.macros }
    val target = category
    val onKnobs = target == CustomizeCategory.QUICK_KNOBS
    val nudge = LocalTourNudge.current
    val randomize = {
        nudge(GestureTip.SWIRL_RANDOMIZE)
        if (category == CustomizeCategory.QUICK_KNOBS) {
            // The knobs' turns, as before the bank (#1001): free, and native rolls them (core/viz/MacroBank).
            macroStore.rollValues(viewModel.vizState.value.sceneId)
        } else {
            val rolled = visualsViewModel.randomizeParams(category, basicsOnly = !proLock.unlocked)
            if (!rolled && !proLock.unlocked) proLock.onLocked()
        }
    }
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Text(
            if (changed == 0) {
                stringResource(R.string.customize_summary_as_styled)
            } else {
                pluralStringResource(R.plurals.customize_summary_changed, changed, changed)
            },
            modifier = Modifier.weight(1f).padding(start = GeodeSpacing.M),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        IconButton(onClick = { onSearching(!searching) }) {
            Icon(
                if (searching) Icons.Filled.Close else Icons.Filled.Search,
                contentDescription =
                    stringResource(if (searching) R.string.customize_a11y_close_search else R.string.customize_a11y_search),
            )
        }
        IconButton(onClick = randomize, enabled = canRoll, modifier = Modifier.tourTarget(TourSpot.CUSTOMIZE_RANDOMIZE)) {
            Icon(Icons.Filled.Casino, contentDescription = stringResource(R.string.customize_a11y_randomize, category.title))
        }
        IconButton(onClick = visualsViewModel::undoParams, enabled = history.canUndo) {
            Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = stringResource(R.string.customize_a11y_undo))
        }
        Box {
            IconButton(onClick = { menuOpen = true }) {
                Icon(Icons.Filled.MoreVert, contentDescription = stringResource(R.string.customize_a11y_more_options))
            }
            DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.action_redo)) },
                    enabled = history.canRedo,
                    leadingIcon = { Icon(Icons.AutoMirrored.Filled.Redo, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        visualsViewModel.redoParams()
                    },
                )
                // The old category list's dice: every category at once, now that a tab is always open.
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.customize_a11y_randomize_all)) },
                    leadingIcon = { Icon(Icons.Filled.Casino, contentDescription = null) },
                    onClick = {
                        menuOpen = false
                        nudge(GestureTip.SWIRL_RANDOMIZE)
                        val rolled = visualsViewModel.randomizeParams(null, basicsOnly = !proLock.unlocked)
                        if (!rolled && !proLock.unlocked) proLock.onLocked()
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.customize_reset_named, target.title)) },
                    onClick = {
                        menuOpen = false
                        if (onKnobs) macroStore.centreAll() else visualsViewModel.resetCustomizeCategory(target)
                    },
                )
                if (!onKnobs) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.customize_take_from_preset, target.title)) },
                        onClick = {
                            menuOpen = false
                            if (proLock.unlocked) borrowing = true else proLock.onLocked()
                        },
                    )
                    // The engine expands the marker to the category's controls (core/viz/CustomizeMap).
                    val (kept, toggleKeep) = LocalParamLocks.current
                    val keepKey = "#${target.name}"
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.customize_keep_named, target.title)) },
                        trailingIcon = { Checkbox(checked = keepKey in kept, onCheckedChange = null) },
                        onClick = {
                            menuOpen = false
                            toggleKeep(keepKey)
                        },
                    )
                }
                DropdownMenuItem(
                    text = {
                        Text(
                            if (changed == 0) {
                                stringResource(R.string.customize_reset_everything)
                            } else {
                                stringResource(R.string.customize_reset_everything_changed, changed)
                            },
                        )
                    },
                    enabled = changed > 0,
                    onClick = {
                        menuOpen = false
                        confirmReset = true
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.customize_save_as_preset)) },
                    onClick = {
                        menuOpen = false
                        savingPreset = true
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.customize_show_tips)) },
                    trailingIcon = { Checkbox(checked = showTips, onCheckedChange = null) },
                    onClick = {
                        menuOpen = false
                        onShowTips(!showTips)
                    },
                )
            }
        }
    }
    if (borrowing) {
        BorrowCategoryDialog(
            category = target,
            viewModel = viewModel,
            onPick = { from ->
                borrowing = false
                visualsViewModel.borrowCustomizeCategory(target, from)
            },
            onDismiss = { borrowing = false },
        )
    }
    if (savingPreset) {
        SavePresetDialog(
            onDismiss = { savingPreset = false },
            onSave = { name ->
                // The one save path: the same call the Presets page makes.
                visualsViewModel.savePreset(name, visualizerView.stage.customShaderFor(viewModel.vizState.value.sceneId))
                savingPreset = false
            },
        )
    }
    if (confirmReset) {
        val sounds = LocalUiSounds.current
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.customize_reset_everything_title)) },
            text = {
                Text(
                    stringResource(R.string.customize_reset_everything_text, changed),
                )
            },
            // Dialog actions are text buttons (M3); the confirm keeps the old button's confirm sound.
            confirmButton = {
                TextButton(onClick = {
                    sounds.play(UiSound.CONFIRM)
                    visualsViewModel.resetAllCustomize()
                    confirmReset = false
                }) { Text(stringResource(R.string.action_reset)) }
            },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.action_cancel)) } },
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
        title = { Text(stringResource(R.string.customize_save_as_preset)) },
        text = {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                singleLine = true,
                placeholder = { Text(stringResource(R.string.preset_name)) },
            )
        },
        confirmButton = {
            TextButton(enabled = name.isNotBlank(), onClick = {
                sounds.play(UiSound.CONFIRM)
                onSave(name.trim())
            }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/**
 * ⋮ › Take a category from a preset: MilkDrop's mash-up, one part of another look laid over this
 * one. Only the list is drawn here; which fields the category owns is native.
 */
@Composable
private fun BorrowCategoryDialog(
    category: CustomizeCategory,
    viewModel: PlayerViewModel,
    onPick: (SceneParams) -> Unit,
    onDismiss: () -> Unit,
) {
    val presets by remember(viewModel) {
        viewModel.vizState.map { it.presets }.distinctUntilChanged()
    }.collectAsStateWithLifecycle(remember(viewModel) { viewModel.vizState.value.presets })
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.customize_take_from, category.title)) },
        text = {
            if (presets.isEmpty()) {
                Text(stringResource(R.string.customize_take_none))
            } else {
                LazyColumn(Modifier.heightIn(max = BORROW_LIST_MAX_HEIGHT)) {
                    items(presets) { preset ->
                        DropdownMenuItem(text = { Text(preset.name) }, onClick = { onPick(preset.params) })
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

private val BORROW_LIST_MAX_HEIGHT = 360.dp
