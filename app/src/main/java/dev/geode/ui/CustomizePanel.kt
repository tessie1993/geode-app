package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Casino
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
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

/**
 * Visuals › Customize: Macros, Look, the style's own page, Events, then Advanced.
 *
 * WHY this order (docs/research/REACTIVE_VISUALS.md §7): the people this is for love music and
 * pictures, not parameter lists. The simple pages come first and hold a handful of plain controls
 * each; everything that needs the words "envelope" or "shader" sits behind Advanced's cards.
 *
 * The toolbar is pinned above the scrolling controls, so Randomize, Undo and search are one tap
 * away however far down a page the user has scrolled. Search still reaches every page at once:
 * "where did the vignette slider go" is exactly the question it exists to answer.
 *
 * The free tier is applied here once ([ProLock]): Macros in full, one headline control on each
 * other simple page (dev.geode.pro.FreeTier), nothing in Advanced. Trial users get everything.
 */
@Composable
internal fun CustomizePanel(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    // Saved by name: the style page's title changes with the style, its name does not.
    var selectedName by rememberSaveable { mutableStateOf(CustomizeTab.MACROS.name) }
    var query by rememberSaveable { mutableStateOf("") }
    var searching by rememberSaveable { mutableStateOf(false) }
    val tabs = CustomizeTab.PANEL.filter { it != CustomizeTab.STYLE || styleTabApplies(viz.sceneId) }
    val selected = tabs.firstOrNull { it.name == selectedName } ?: tabs.first()
    val titles = tabs.map { if (it == CustomizeTab.STYLE) sceneDisplayLabel(viz.sceneId) else it.title }
    val unlocked = gate.allows(ProFeature.ADVANCED_CUSTOMIZATION)
    val proLock =
        remember(unlocked, viz.sceneId, paywall) {
            ProLock(
                unlocked = unlocked,
                free = FreeTier.freeCustomizeControls { key -> styleControlShown(key, viz.sceneId) },
                onLocked = { paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION)) },
            )
        }
    // Back steps to the first page before it leaves Customize; an open Advanced card, composed
    // later, closes before this (VisualsNavState has the whole order).
    BackHandler(enabled = selected != tabs.first()) { selectedName = tabs.first().name }
    Column(Modifier.fillMaxSize()) {
        CrystalTabs(
            titles = titles,
            selected = tabs.indexOf(selected),
            onSelect = { selectedName = tabs[it].name },
        )
        CustomizeToolbar(
            viewModel = viewModel,
            visualizerView = visualizerView,
            params = viz.params,
            tab = selected,
            proLock = proLock,
            searching = searching,
            onSearching = { on ->
                searching = on
                if (!on) query = ""
            },
            query = query,
            onQuery = { query = it },
        )
        Column(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
        ) {
            val locked by visualsViewModel.lockedParams.collectAsStateWithLifecycle()
            CompositionLocalProvider(
                LocalParamLocks provides (locked to visualsViewModel::toggleParamLock),
                LocalProLock provides proLock,
            ) {
                ParamSurface(sceneId = viz.sceneId, query = query) {
                    if (query.isBlank()) {
                        CustomizeTabBody(viewModel, visualizerView, selected)
                    } else {
                        // Macros' knobs are not searchable controls; every other page is.
                        tabs.filter { it != CustomizeTab.MACROS }.forEach { CustomizeTabBody(viewModel, visualizerView, it) }
                    }
                }
            }
        }
    }
}

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
private fun CustomizeTabBody(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    tab: CustomizeTab,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (SceneParams) -> Unit = visualsViewModel::editSceneParams
    when (tab) {
        CustomizeTab.MACROS -> {
            val context = LocalContext.current
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            val proLock = LocalProLock.current
            MacrosTab(macros, store, canSavePresets = proLock.unlocked, onLockedSave = proLock.onLocked)
        }
        CustomizeTab.LOOK -> {
            val artNote by viewModel.artPaletteNote.collectAsStateWithLifecycle()
            LookTab(p, onChange, onTakeArtworkPalette = viewModel::applyArtworkPalette, artworkNote = artNote)
        }
        CustomizeTab.STYLE -> StyleTab(p, onChange)
        CustomizeTab.EVENTS -> {
            val reactions by visualsViewModel.reactions.collectAsStateWithLifecycle()
            EventsTab(p, onChange, reactions = reactions, onReactionsChange = visualsViewModel::setReactions)
        }
        CustomizeTab.ADVANCED -> {
            val proLock = LocalProLock.current
            AdvancedTab(
                AdvancedHost(viewModel, visualsViewModel, visualizerView),
                unlocked = proLock.unlocked,
                onLocked = proLock.onLocked,
            )
        }
        // The engine's roll groups are not pages of their own.
        else -> Unit
    }
}

/** What the toolbar's Randomize is called on [tab]. */
private fun rollLabel(tab: CustomizeTab): String =
    when (tab) {
        CustomizeTab.MACROS -> "Surprise"
        CustomizeTab.EVENTS -> "Shuffle reactions"
        else -> "Randomize"
    }

/**
 * The sticky row above the controls: Randomize (Surprise on Macros, free; Pro elsewhere), Undo,
 * Redo, search, and an overflow with the resets and "Save as preset". Icon buttons keep their full
 * 48dp target and carry their own names for TalkBack.
 */
@Composable
private fun CustomizeToolbar(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    params: SceneParams,
    tab: CustomizeTab,
    proLock: ProLock,
    searching: Boolean,
    onSearching: (Boolean) -> Unit,
    query: String,
    onQuery: (String) -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val history by visualsViewModel.paramHistory.collectAsStateWithLifecycle()
    var menuOpen by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    var savingPreset by remember { mutableStateOf(false) }
    val changed = remember(params) { CustomizeSummary.changedCount(params) }
    val context = LocalContext.current
    val macroStore = remember { MacroStore.get(context) }
    val onMacros = tab == CustomizeTab.MACROS
    val rollLocked = !onMacros && !proLock.unlocked
    Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            CrystalButton(
                compact = true,
                onClick = {
                    when {
                        onMacros -> macroStore.surprise()
                        rollLocked -> proLock.onLocked()
                        else -> visualsViewModel.randomizeParams(tab)
                    }
                },
            ) {
                Icon(Icons.Filled.Casino, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(6.dp))
                Text(rollLabel(tab))
                if (rollLocked) {
                    Spacer(Modifier.width(4.dp))
                    Icon(Icons.Filled.Lock, contentDescription = "Pro", modifier = Modifier.size(16.dp))
                }
            }
            Spacer(Modifier.weight(1f))
            IconButton(onClick = visualsViewModel::undoParams, enabled = history.canUndo) {
                Icon(Icons.AutoMirrored.Filled.Undo, contentDescription = "Undo")
            }
            IconButton(onClick = visualsViewModel::redoParams, enabled = history.canRedo) {
                Icon(Icons.AutoMirrored.Filled.Redo, contentDescription = "Redo")
            }
            IconButton(onClick = { onSearching(!searching) }) {
                Icon(
                    if (searching) Icons.Filled.Close else Icons.Filled.Search,
                    contentDescription = if (searching) "Close search" else "Search every control",
                )
            }
            Box {
                IconButton(onClick = { menuOpen = true }) {
                    Icon(Icons.Filled.MoreVert, contentDescription = "More options")
                }
                DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                    DropdownMenuItem(
                        text = { Text("Reset this tab") },
                        onClick = {
                            menuOpen = false
                            if (onMacros) macroStore.resetKnobs() else visualsViewModel.resetCustomizeTab(tab)
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
                }
            }
        }
        if (searching) {
            OutlinedTextField(
                value = query,
                onValueChange = onQuery,
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("Search every control") },
            )
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
                    "Puts all $changed changed controls back to their defaults, on every tab. " +
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
