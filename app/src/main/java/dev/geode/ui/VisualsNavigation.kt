package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import androidx.navigation3.runtime.NavKey
import dev.geode.R

/**
 * The pages of the Visuals panel, in tab order. [STYLES] is always the root of the route and
 * every other page sits on it, so system back retraces the route to the styles before it leaves
 * Visuals. [TEXTURES] has no tab: MilkDrop opens it, since textures only matter to MilkDrop presets.
 */
internal enum class VisualsPage(
    @param:StringRes val titleRes: Int,
) {
    STYLES(R.string.visuals_tab_styles),
    CUSTOMIZE(R.string.visuals_tab_customize),
    PRESETS(R.string.visuals_tab_presets),
    MILKDROP(R.string.visuals_tab_milkdrop),
    TEXTURES(R.string.visuals_tab_textures),
    SETTINGS(R.string.visuals_qs_title),
}

internal val VisualsTabs = listOf(VisualsPage.STYLES, VisualsPage.CUSTOMIZE, VisualsPage.PRESETS, VisualsPage.MILKDROP)

/**
 * One step of the route through Visuals: a page, or the Customize category open on it. Each has
 * exactly one key, so the route ([VisualsNavState.route]) says where the user is down to the open
 * category, and back can undo each level in turn.
 *
 * A category carries its name as a string, not an enum, so a saved route only ever needs the name.
 * A name the panel no longer has (a route saved by the old seven-card panel, or Camera on a flat
 * style) is the panel's to skip. Which sections are open inside a category is not a step: several
 * can be open at once, so the panel keeps them by name beside the route ([rememberOpenSections]).
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

    /**
     * The open Customize category (Quick knobs, Look, Shape …, [CustomizeCategory]), by its name.
     * One is open at a time, so there is at most one of these on the route: ‹ › replace it and back
     * pops it, which is what lets back close the category first and come back to it from another tab.
     */
    data class CustomizeCard(
        val name: String,
    ) : VisualsKey {
        override val page: VisualsPage get() = VisualsPage.CUSTOMIZE
        override val id: String get() = "$CUSTOMIZE_PREFIX$name"
    }

    companion object {
        private const val CUSTOMIZE_PREFIX = "customize/"

        val root: VisualsKey = Page(VisualsPage.STYLES)

        fun fromId(id: String): VisualsKey? =
            when {
                id.startsWith(CUSTOMIZE_PREFIX) -> id.removePrefix(CUSTOMIZE_PREFIX).takeIf { it.isNotEmpty() }?.let(::CustomizeCard)
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
                // One Customize card at a time; a second one in an older route is dropped.
                val oneCard = key !is CustomizeCard || route.none { it is CustomizeCard }
                if (fits && oneCard && key !in route) route.add(key)
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
 * How much of the options sheet shows over the full-screen visual. [PEEK] is its header alone
 * (the tabs), [HALF] and [TALL] the page under it, [HIDDEN] the visual alone. Saved by name.
 */
internal enum class SheetHeight { HIDDEN, PEEK, HALF, TALL }

/**
 * Every navigation decision of the Visuals tab in one place: the route the user took through the
 * options (a [NavHistory] of [VisualsKey]s), how high the options sheet stands over the visual,
 * and what back undoes first. The screens only call these, so moving Visuals onto a top-level
 * NavDisplay later (ADR 0001) rewires this class and [VisualsHub], nothing else.
 *
 * The route is true history: opening a tab goes forward from wherever the user is, it does not
 * clear back to Styles, so back retraces the way they came (Styles → Presets → Customize, back,
 * Presets, back, Styles). [NavHistory] says why a page already on the route is returned to rather
 * than added twice. Customize's open category has a key of its own ([openCustomizeCard]); its ‹ ›
 * arrows go sideways ([stepCustomizeCard]) and add no back entries.
 *
 * The visual always fills the screen (docs/ui-redesign); the options are a floating sheet over
 * it whose height is [sheet]. In landscape the same state drives the side panel instead
 * (V2 rule 5): any shown height means the panel is out, [SheetHeight.HIDDEN] means it is away, so
 * turning the phone keeps the route, the page and whether the options are up.
 *
 * Back order, most recent first ([back] runs it whole): a hidden sheet
 * comes back at its last height, then the route in reverse (the open Customize category, then the
 * page before it), then the shell
 * leaves Visuals.
 */
@Stable
internal class VisualsNavState(
    val route: SnapshotStateList<VisualsKey>,
    sheetState: MutableState<SheetHeight>,
    lastShownState: MutableState<SheetHeight>,
) {
    private val history = NavHistory(route)

    var sheet by sheetState
        private set

    /** The height the sheet comes back to when it is shown again. */
    private var lastShown by lastShownState

    /**
     * The pages the panel's NavDisplay draws: the route with each sub-page folded into its page.
     * Each page is on the route once and its sub-pages sit right on it, so no page repeats here.
     *
     * Folded means NavDisplay's predictive pop only ever previews a page leaving. So the open
     * Customize category takes the gesture before NavDisplay does (`rememberCardBack` in
     * CustomizeBrowser.kt, issue #557): its preview is the card fading back into the category list,
     * and its commit is [closeCustomizeCard]'s step, the same one [pop] would take.
     */
    val backStack: List<VisualsPage> by derivedStateOf {
        buildList { route.forEach { key -> if (lastOrNull() != key.page) add(key.page) } }
    }

    /** The page on screen, under any sub-page open on it. */
    val top: VisualsPage get() = history.top.page

    /** The Customize category open on the route, if one is. */
    val openCard: String? get() = route.filterIsInstance<VisualsKey.CustomizeCard>().lastOrNull()?.name

    /**
     * The lit tab: Textures belongs to MilkDrop (it opens it), so MilkDrop stays lit there; Settings
     * has no tab, so the tab it was opened over stays lit.
     */
    val selectedTab: Int
        get() =
            route
                .map { if (it.page == VisualsPage.TEXTURES) VisualsPage.MILKDROP else it.page }
                .lastOrNull { it in VisualsTabs }
                .let { VisualsTabs.indexOf(it ?: VisualsPage.STYLES) }

    val sheetShown: Boolean get() = sheet != SheetHeight.HIDDEN

    /** A tab: forward to its page, on top of the route so far (or back to it, if it is on it). */
    fun openTab(page: VisualsPage) {
        history.open(VisualsKey.Page(page))
        // A page wants room to show: a sheet down to its tabs rises to half.
        if (sheet == SheetHeight.PEEK || sheet == SheetHeight.HIDDEN) settleSheet(SheetHeight.HALF)
    }

    /** A page with no tab of its own (Textures), on top of the one that opened it. */
    fun push(page: VisualsPage) {
        openTab(page)
    }

    /**
     * Opens the Customize category [name], on Customize (which opens first if it is not on screen).
     * The category that was open is replaced, not stacked, so one open category is one back step.
     */
    fun openCustomizeCard(name: String) {
        onPage(VisualsPage.CUSTOMIZE)
        if (history.top is VisualsKey.CustomizeCard) history.pop()
        history.open(VisualsKey.CustomizeCard(name))
    }

    /** Closes the open Customize category: the same step back as system back takes. */
    fun closeCustomizeCard() {
        if (history.top is VisualsKey.CustomizeCard) history.pop()
    }

    /**
     * Customize's ‹ ›: the open category becomes its neighbour [delta] places along [order] (the
     * categories the list shows, in its order). It replaces the open one, so back still returns to
     * the list in one step, and it stops at either end instead of wrapping. True when it moved.
     */
    fun stepCustomizeCard(
        order: List<String>,
        delta: Int,
    ): Boolean = history.step(order.map(VisualsKey::CustomizeCard), delta)

    /** One step back along the route; false at Styles, where back leaves Visuals. */
    fun pop(): Boolean = history.pop()

    /** The whole back order in one call; false when there is nothing left, so the shell leaves. */
    fun back(): Boolean =
        if (!sheetShown) {
            showSheet()
            true
        } else {
            pop()
        }

    /** The sheet at [height]; a shown height is remembered for the next [showSheet]. */
    fun settleSheet(height: SheetHeight) {
        sheet = height
        if (height != SheetHeight.HIDDEN) lastShown = height
    }

    fun showSheet() = settleSheet(lastShown)

    fun hideSheet() = settleSheet(SheetHeight.HIDDEN)

    /** Opens [page] unless it is already the page on screen, keeping the sub-pages open on it. */
    private fun onPage(page: VisualsPage) {
        if (top != page) history.open(VisualsKey.Page(page))
    }
}

/** Saved by name, so a reordered enum cannot restore the wrong height. */
private val SheetHeightSaver =
    Saver<MutableState<SheetHeight>, String>(
        save = { it.value.name },
        restore = { name -> mutableStateOf(SheetHeight.entries.firstOrNull { it.name == name } ?: SheetHeight.HALF) },
    )

@Composable
internal fun rememberVisualsNavState(): VisualsNavState {
    val route = rememberSaveable(saver = VisualsRouteSaver) { mutableStateListOf(VisualsKey.root) }
    val sheet = rememberSaveable(saver = SheetHeightSaver) { mutableStateOf(SheetHeight.HALF) }
    val lastShown = rememberSaveable(saver = SheetHeightSaver) { mutableStateOf(SheetHeight.HALF) }
    return remember(route) { VisualsNavState(route, sheet, lastShown) }
}
