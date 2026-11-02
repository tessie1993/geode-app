package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import androidx.navigation3.runtime.NavKey

/**
 * The pages of the Visuals panel, in tab order. [STYLES] is always the root of the route and
 * every other page sits on it, so system back retraces the route to the styles before it leaves
 * Visuals. [TEXTURES] has no tab: MilkDrop opens it, since textures only matter to MilkDrop presets.
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

/**
 * One step of the route through Visuals: a page, or a sub-page inside one. Each page and each
 * sub-page has exactly one key, so the route ([VisualsNavState.route]) says where the user is
 * down to the open Advanced group, and back can undo each level in turn.
 *
 * Sub-pages carry their name as a string, not an enum: Customize's pages and Advanced's groups
 * are being regrouped (3D) and their enums will change, while a saved route only ever needs the
 * name. A name the panel no longer has is the panel's to skip, the way it skips a page that does
 * not apply to the style on screen.
 *
 * [id] is how a key is saved. A page saves as its bare name, the format of the old page-only
 * back stack, so a route saved by an older build restores as it was.
 */
internal sealed interface VisualsKey : NavKey {
    /** The panel page this key is drawn on. */
    val page: VisualsPage

    val id: String

    data class Page(
        override val page: VisualsPage,
    ) : VisualsKey {
        override val id: String get() = page.name
    }

    /** A Customize page (Macros, Look, the style's own, Events, Advanced), by its name. */
    data class CustomizeSub(
        val name: String,
    ) : VisualsKey {
        override val page: VisualsPage get() = VisualsPage.CUSTOMIZE
        override val id: String get() = "$CUSTOMIZE_PREFIX$name"
    }

    /** An open Advanced group (Camera, Liquid, …), by its name; stepped by the ‹ › arrows. */
    data class AdvancedGroup(
        val name: String,
    ) : VisualsKey {
        override val page: VisualsPage get() = VisualsPage.CUSTOMIZE
        override val id: String get() = "$ADVANCED_PREFIX$name"
    }

    companion object {
        private const val CUSTOMIZE_PREFIX = "customize/"
        private const val ADVANCED_PREFIX = "advanced/"

        val root: VisualsKey = Page(VisualsPage.STYLES)

        fun fromId(id: String): VisualsKey? =
            when {
                id.startsWith(CUSTOMIZE_PREFIX) -> id.removePrefix(CUSTOMIZE_PREFIX).takeIf { it.isNotEmpty() }?.let(::CustomizeSub)
                id.startsWith(ADVANCED_PREFIX) -> id.removePrefix(ADVANCED_PREFIX).takeIf { it.isNotEmpty() }?.let(::AdvancedGroup)
                else -> VisualsPage.entries.firstOrNull { it.name == id }?.let(::Page)
            }

        /**
         * The route as [VisualsNavState] keeps it: Styles first, each key once, and a sub-page
         * only directly on its own page (or on another sub-page of it). Whatever breaks that, from
         * an older build or a renamed page, is dropped rather than restored into a route back
         * could not walk.
         */
        fun sanitize(keys: List<VisualsKey>): List<VisualsKey> {
            val route = mutableListOf(root)
            keys.drop(if (keys.firstOrNull() == root) 1 else keys.size).forEach { key ->
                val fits = key is Page || route.last().page == key.page
                if (fits && key !in route) route.add(key)
            }
            return route
        }
    }
}

/** Saved by id, like the shell's destination, so reordering the pages cannot restore the wrong one. */
private val VisualsRouteSaver =
    listSaver<SnapshotStateList<VisualsKey>, String>(
        save = { route -> route.map { it.id } },
        restore = { ids -> VisualsKey.sanitize(ids.mapNotNull(VisualsKey::fromId)).toMutableStateList() },
    )

/**
 * Every navigation decision of the Visuals tab in one place: the route the user took through the
 * panel (a [NavHistory] of [VisualsKey]s), the bars a swipe hid, the panel a hold hid, and what
 * back undoes first. The screens only call these, so moving Visuals onto a top-level NavDisplay
 * later (ADR 0001) rewires this class and [VisualsHub], nothing else.
 *
 * The route is true history: opening a tab goes forward from wherever the user is, it does not
 * clear back to Styles, so back retraces the way they came (Styles → Presets → Customize, back,
 * Presets, back, Styles). [NavHistory] says why a page already on the route is returned to rather
 * than added twice. Customize's pages and Advanced's groups have keys of their own
 * ([openCustomizePage], [openAdvancedGroup]); Advanced's ‹ › arrows go sideways ([stepAdvanced])
 * and add no back entries. Customize still keeps its open page and card itself until its regroup
 * (3D) moves them onto these keys; nothing here changes what it does today.
 *
 * Back order, most recent first ([back] runs it whole): hidden bars ([barsBackEnabled]), a hidden
 * panel ([panelBackEnabled]), then the route in reverse, sub-pages first, then the shell leaves
 * Visuals.
 */
@Stable
internal class VisualsNavState(
    val route: SnapshotStateList<VisualsKey>,
    barsHiddenState: MutableState<Boolean>,
    customizeBarsShownState: MutableState<Boolean>,
    panelShownState: MutableState<Boolean>,
) {
    private val history = NavHistory(route)

    // What a swipe hid. Customize keeps its own, because it starts hidden every time it opens.
    private var barsHidden by barsHiddenState
    private var customizeBarsShown by customizeBarsShownState
    var panelShown by panelShownState
        private set

    /**
     * The pages the panel's NavDisplay draws: the route with each sub-page folded into its page.
     * Each page is on the route once and its sub-pages sit right on it, so no page repeats here.
     */
    val backStack: List<VisualsPage> by derivedStateOf {
        buildList { route.forEach { key -> if (lastOrNull() != key.page) add(key.page) } }
    }

    /** The page on screen, under any sub-page open on it. */
    val top: VisualsPage get() = history.top.page

    private val onCustomize: Boolean get() = top == VisualsPage.CUSTOMIZE

    /** The Customize page open on the route, if one is; the last one opened when there are several. */
    val customizePage: String? get() = route.filterIsInstance<VisualsKey.CustomizeSub>().lastOrNull()?.name

    /** The Advanced group on screen, if one is. */
    val advancedGroup: String? get() = (history.top as? VisualsKey.AdvancedGroup)?.name

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

    /** Whether [back] has anything to undo; false means back leaves Visuals. */
    val backEnabled: Boolean get() = barsBackEnabled || panelBackEnabled || history.canPop

    /** A tab: forward to its page, on top of the route so far (or back to it, if it is on it). */
    fun openTab(page: VisualsPage) {
        go { history.open(VisualsKey.Page(page)) }
        // Tapping Customize while on it starts it over too, as it always has.
        if (page == VisualsPage.CUSTOMIZE) customizeBarsShown = false
    }

    /** A page with no tab of its own (Textures), on top of the one that opened it. */
    fun push(page: VisualsPage) {
        openTab(page)
    }

    /** A Customize page, opened on Customize (which opens first if it is not on screen). */
    fun openCustomizePage(name: String) {
        go {
            onPage(VisualsPage.CUSTOMIZE)
            history.open(VisualsKey.CustomizeSub(name))
        }
    }

    /** An Advanced group, opened from the Advanced list, so back returns to the list. */
    fun openAdvancedGroup(name: String) {
        go {
            onPage(VisualsPage.CUSTOMIZE)
            history.open(VisualsKey.AdvancedGroup(name))
        }
    }

    /**
     * Advanced's ‹ › arrows: the open group becomes its neighbour [delta] places along [order]
     * (the groups the list shows, in its order). It replaces the open group, so back still returns
     * to the list in one step, and it stops at either end instead of wrapping. True when it moved.
     */
    fun stepAdvanced(
        order: List<String>,
        delta: Int,
    ): Boolean = history.step(order.map(VisualsKey::AdvancedGroup), delta)

    /** Where [stepAdvanced] would land, or null at an end; for the arrows' enabled state and labels. */
    fun advancedNeighbour(
        order: List<String>,
        delta: Int,
    ): String? = (history.neighbour(order.map(VisualsKey::AdvancedGroup), delta) as? VisualsKey.AdvancedGroup)?.name

    /** One step back along the route; false at Styles, where back leaves Visuals. */
    fun pop(): Boolean = go { history.pop() }

    /** The whole back order in one call; false when there is nothing left, so the shell leaves. */
    fun back(): Boolean =
        when {
            barsBackEnabled -> {
                backFromBars()
                true
            }
            panelBackEnabled -> {
                backFromPanel()
                true
            }
            else -> pop()
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

    /** Opens [page] unless it is already the page on screen, keeping the sub-pages open on it. */
    private fun onPage(page: VisualsPage) {
        if (top != page) history.open(VisualsKey.Page(page))
    }

    /** Runs a route change; Customize, arrived at from another page, opens with its bars hidden. */
    private inline fun <R> go(change: () -> R): R {
        val before = top
        val result = change()
        if (top == VisualsPage.CUSTOMIZE && before != VisualsPage.CUSTOMIZE) customizeBarsShown = false
        return result
    }
}

@Composable
internal fun rememberVisualsNavState(): VisualsNavState {
    val route = rememberSaveable(saver = VisualsRouteSaver) { mutableStateListOf(VisualsKey.root) }
    val barsHidden = rememberSaveable { mutableStateOf(false) }
    val customizeBarsShown = rememberSaveable { mutableStateOf(false) }
    val panelShown = rememberSaveable { mutableStateOf(true) }
    return remember(route) { VisualsNavState(route, barsHidden, customizeBarsShown, panelShown) }
}
