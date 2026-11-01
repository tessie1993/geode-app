package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList

/**
 * The pages of the Visuals panel, in tab order. [STYLES] is always the root of the back stack and
 * every other page sits on it, so system back steps toward the styles before it leaves Visuals.
 * [TEXTURES] has no tab: MilkDrop opens it, since textures only matter to MilkDrop presets.
 */
internal enum class VisualsPage(
    val title: String,
) {
    STYLES("Styles"),
    CUSTOMIZE("Customize"),
    PRESETS("Presets"),
    MILKDROP("MilkDrop"),
    TEXTURES("Textures"),
}

internal val VisualsTabs = listOf(VisualsPage.STYLES, VisualsPage.CUSTOMIZE, VisualsPage.PRESETS, VisualsPage.MILKDROP)

/** Saved by name, like the shell's destination, so reordering the pages cannot restore the wrong one. */
private val VisualsBackStackSaver =
    listSaver<SnapshotStateList<VisualsPage>, String>(
        save = { stack -> stack.map { it.name } },
        restore = { names ->
            val pages = names.mapNotNull { n -> VisualsPage.entries.firstOrNull { it.name == n } }
            // Styles is always the root; a stack saved by an older build without it starts over there.
            (if (pages.firstOrNull() == VisualsPage.STYLES) pages else listOf(VisualsPage.STYLES)).toMutableStateList()
        },
    )

/**
 * Every navigation decision of the Visuals tab in one place: the panel's Navigation 3 back stack,
 * the bars a swipe hid, the panel a hold hid, and what back undoes first. The screens only call
 * these; none of them keeps navigation state of its own (Customize's page and an open Advanced
 * card aside, which are inside a page), so moving Visuals onto a top-level NavDisplay later
 * (ADR 0001) rewires this class and [VisualsHub], nothing else.
 *
 * Back order, most recent first: hidden bars ([barsBackEnabled]), a hidden panel
 * ([panelBackEnabled]), whatever the page has open, then the page itself back to Styles
 * ([backStack]), then the shell leaves Visuals.
 */
@Stable
internal class VisualsNavState(
    val backStack: SnapshotStateList<VisualsPage>,
    barsHiddenState: MutableState<Boolean>,
    customizeBarsShownState: MutableState<Boolean>,
    panelShownState: MutableState<Boolean>,
) {
    // What a swipe hid. Customize keeps its own, because it starts hidden every time it opens.
    private var barsHidden by barsHiddenState
    private var customizeBarsShown by customizeBarsShownState
    var panelShown by panelShownState
        private set

    val top: VisualsPage get() = backStack.last()

    private val onCustomize: Boolean get() = top == VisualsPage.CUSTOMIZE

    /** The lit tab: Textures belongs to MilkDrop (it opens it), so MilkDrop stays lit there. */
    val selectedTab: Int get() = VisualsTabs.indexOf(if (top == VisualsPage.TEXTURES) VisualsPage.MILKDROP else top).coerceAtLeast(0)

    /** What the shell draws around the stage; see [VisualsChrome]. */
    val chrome: VisualsChrome
        get() =
            when {
                onCustomize -> if (customizeBarsShown) VisualsChrome.MINIMAL else VisualsChrome.HIDDEN
                barsHidden -> VisualsChrome.HIDDEN
                else -> VisualsChrome.FULL
            }

    val barsBackEnabled: Boolean get() = barsHidden && !onCustomize

    val panelBackEnabled: Boolean get() = !panelShown

    /** A tab: the page goes straight on Styles, so back from any tab is one step. */
    fun openTab(page: VisualsPage) {
        while (backStack.size > 1) backStack.removeAt(backStack.lastIndex)
        if (page != VisualsPage.STYLES) backStack.add(page)
        if (page == VisualsPage.CUSTOMIZE) customizeBarsShown = false
    }

    /** A page with no tab of its own (Textures), on top of the one that opened it. */
    fun push(page: VisualsPage) {
        backStack.add(page)
    }

    fun pop() {
        if (backStack.size > 1) backStack.removeAt(backStack.lastIndex)
    }

    fun showBars(show: Boolean) {
        if (onCustomize) customizeBarsShown = show else barsHidden = !show
    }

    fun togglePanel() {
        panelShown = !panelShown
    }

    fun backFromBars() {
        barsHidden = false
    }

    fun backFromPanel() {
        panelShown = true
    }
}

@Composable
internal fun rememberVisualsNavState(): VisualsNavState {
    val backStack = rememberSaveable(saver = VisualsBackStackSaver) { mutableStateListOf(VisualsPage.STYLES) }
    val barsHidden = rememberSaveable { mutableStateOf(false) }
    val customizeBarsShown = rememberSaveable { mutableStateOf(false) }
    val panelShown = rememberSaveable { mutableStateOf(true) }
    return remember(backStack) { VisualsNavState(backStack, barsHidden, customizeBarsShown, panelShown) }
}
