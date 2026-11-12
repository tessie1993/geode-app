package dev.geode.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.UnfoldLess
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.model.GestureTip
import dev.geode.model.TourSpot
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.edgeFade
import dev.geode.ui.tour.LocalTourNudge
import dev.geode.ui.tour.tourTarget

/**
 * Customize as a row of category tabs over collapsible groups (owner, 2026-11-12).
 *
 * WHY tabs: the list-then-page drill-down (issue #333) cost a tap and a back for every change of
 * category; a scrollable tab row makes it one tap and keeps every category in sight. A tab is not a
 * back step, as ‹ › were not: back from Customize leaves it. WHY every group collapsed at first: a
 * tab reads as a list of named groups (Basics, Colour detail, Lens …, the native map's sections), and
 * the user opens what they came for; what they open stays open ([openGroups], kept by the caller in
 * `CustomizeGroupsStore`), several at once, and Collapse all (beside the tabs, and a TalkBack action
 * on every group) closes the tab's groups in one go.
 *
 * Stateless: the caller owns which tab is [selected] and which groups are open, and [sectionContent]
 * draws a group's controls, so the panel and the Compose test share this exact browser.
 */
@Composable
internal fun CustomizeBrowser(
    categories: List<CustomizeCategory>,
    selected: CustomizeCategory,
    onSelect: (CustomizeCategory) -> Unit,
    sectionsOf: (CustomizeCategory) -> List<CustomizeSection>,
    openGroups: Set<String>,
    onToggleGroup: (CustomizeSection) -> Unit,
    onCollapseAll: (CustomizeCategory) -> Unit,
    sectionLocked: (CustomizeSection) -> Boolean,
    modifier: Modifier = Modifier,
    header: @Composable (CustomizeCategory) -> Unit = {},
    sectionContent: @Composable (CustomizeSection) -> Unit,
) {
    val sections = sectionsOf(selected)
    val anyOpen = sections.any { it.path in openGroups }
    val collapseAll = { onCollapseAll(selected) }
    Column(modifier) {
        header(selected)
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            CategoryTabs(categories, selected, onSelect, Modifier.weight(1f).tourTarget(TourSpot.CUSTOMIZE_LIST))
            IconButton(onClick = collapseAll, enabled = anyOpen) {
                Icon(Icons.Filled.UnfoldLess, contentDescription = stringResource(R.string.customize_collapse_all_in, selected.title))
            }
        }
        // One scroll per tab, so a new tab starts at its top.
        key(selected) {
            GroupList(
                sections = sections,
                openGroups = openGroups,
                onToggleGroup = onToggleGroup,
                sectionLocked = sectionLocked,
                onCollapseAll = collapseAll.takeIf { anyOpen },
                sectionContent = sectionContent,
                modifier = Modifier.weight(1f).fillMaxWidth(),
            )
        }
    }
}

/**
 * The category tabs: scrollable, each as wide as its label, the open one lit. The words and icons
 * are [CustomizeCategory]'s; which categories show on the style is native.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CategoryTabs(
    categories: List<CustomizeCategory>,
    selected: CustomizeCategory,
    onSelect: (CustomizeCategory) -> Unit,
    modifier: Modifier,
) {
    val nudge = LocalTourNudge.current
    val scroll = rememberScrollState()
    PrimaryScrollableTabRow(
        selectedTabIndex = categories.indexOf(selected).coerceAtLeast(0),
        modifier = modifier.edgeFade(scroll, horizontal = true),
        scrollState = scroll,
        edgePadding = GeodeSpacing.XS,
        minTabWidth = 0.dp,
        containerColor = Color.Transparent,
        contentColor = accentTextColor(),
        divider = {},
    ) {
        categories.forEach { category ->
            val knobs = category == CustomizeCategory.QUICK_KNOBS
            Tab(
                selected = category == selected,
                onClick = {
                    // The knobs are also one swipe down on the visual, which nothing on screen says.
                    if (knobs) nudge(GestureTip.SWIPE_DOWN_KNOBS)
                    onSelect(category)
                },
                modifier = if (knobs) Modifier.tourTarget(TourSpot.CUSTOMIZE_KNOBS) else Modifier,
                selectedContentColor = accentTextColor(),
                unselectedContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
            ) {
                // The content overload: the text one pads 16 dp a side, and eleven tabs want more of them in view.
                Box(
                    Modifier.heightIn(min = GeodeSpacing.TouchTarget).padding(horizontal = GeodeSpacing.M),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(category.title, style = MaterialTheme.typography.titleSmall, maxLines = 1)
                }
            }
        }
    }
}

@Composable
private fun GroupList(
    sections: List<CustomizeSection>,
    openGroups: Set<String>,
    onToggleGroup: (CustomizeSection) -> Unit,
    sectionLocked: (CustomizeSection) -> Boolean,
    onCollapseAll: (() -> Unit)?,
    sectionContent: @Composable (CustomizeSection) -> Unit,
    modifier: Modifier,
) {
    LazyColumn(
        state = rememberLazyListState(),
        modifier = modifier,
        contentPadding = PaddingValues(start = GeodeSpacing.S, end = GeodeSpacing.S, bottom = GeodeSpacing.XL),
    ) {
        items(sections, key = { it.path }) { section ->
            GroupBlock(
                section = section,
                expanded = section.path in openGroups,
                locked = sectionLocked(section),
                onToggle = { onToggleGroup(section) },
                onCollapseAll = onCollapseAll,
                content = { sectionContent(section) },
            )
        }
    }
}

private const val QUARTER_TURN = 90f

/**
 * A group that drops open downwards: ▸ name (♛ when Pro and locked), its controls under it when
 * open. TalkBack reads the header as a heading with its state, and offers Collapse all on it while
 * any group in the tab is open.
 */
@Composable
private fun GroupBlock(
    section: CustomizeSection,
    expanded: Boolean,
    locked: Boolean,
    onToggle: () -> Unit,
    onCollapseAll: (() -> Unit)?,
    content: @Composable () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val toggleLabel = stringResource(if (expanded) R.string.customize_card_collapse else R.string.customize_card_expand)
    val openState = stringResource(if (expanded) R.string.customize_card_expanded else R.string.customize_card_collapsed)
    val collapseAllLabel = stringResource(R.string.customize_collapse_all)
    Column(Modifier.fillMaxWidth().animateContentSize(motion.defaultSpatialSpec())) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.Button, onClickLabel = toggleLabel, onClick = onToggle)
                .semantics(mergeDescendants = true) {
                    heading()
                    stateDescription = openState
                    if (onCollapseAll != null) {
                        customActions = listOf(CustomAccessibilityAction(collapseAllLabel) { true.also { onCollapseAll() } })
                    }
                },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                Icons.Filled.ExpandMore,
                contentDescription = null,
                modifier = Modifier.rotate(if (expanded) 0f else -QUARTER_TURN),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                section.title,
                style = MaterialTheme.typography.titleSmall,
                color = accentTextColor(),
                modifier = Modifier.padding(start = GeodeSpacing.S),
            )
            if (locked) ProCrown()
        }
        if (expanded) {
            Column(Modifier.fillMaxWidth().padding(start = GeodeSpacing.XS, bottom = GeodeSpacing.XS)) { content() }
        }
    }
}
