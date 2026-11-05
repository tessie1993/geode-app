package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.floatingGlass

/**
 * Where the Customize browser is: which category is open, if any. With the Visuals route
 * ([VisualsNavState]) the open category is a [VisualsKey.CustomizeCard], so system back closes it
 * before leaving; ‹ › step sideways and add no back entry ([NavHistory.step]).
 */
@Stable
internal interface CustomizeNav {
    val openCategory: String?

    fun open(name: String)

    fun close()

    /** The open category becomes its neighbour [delta] places along [order]; stops at either end. */
    fun step(
        order: List<String>,
        delta: Int,
    )
}

/** The browser's own nav, for when no Visuals route is given (tests, previews). Saved by name. */
@Composable
internal fun rememberLocalCustomizeNav(): CustomizeNav {
    val open = rememberSaveable { mutableStateOf<String?>(null) }
    return remember(open) {
        object : CustomizeNav {
            override val openCategory: String? get() = open.value

            override fun open(name: String) {
                open.value = name
            }

            override fun close() {
                open.value = null
            }

            override fun step(
                order: List<String>,
                delta: Int,
            ) {
                val at = order.indexOf(open.value)
                if (at >= 0) order.getOrNull(at + delta)?.let { open.value = it }
            }
        }
    }
}

/** The sections open in each category, as `CATEGORY/id` paths; several at once, saved by name. */
@Composable
internal fun rememberOpenSections(): MutableState<Set<String>> =
    rememberSaveable(saver = OpenSectionsSaver) { mutableStateOf(emptySet()) }

private val OpenSectionsSaver =
    listSaver<MutableState<Set<String>>, String>(
        save = { it.value.toList() },
        restore = { mutableStateOf(it.toSet()) },
    )

/**
 * Customize as a list of categories, then one category at a time (issue #333).
 *
 * WHY progressive disclosure: the old panel opened as seven cards, any one of which could hold
 * thirty controls. Here the top level is ten rows that say what each category does, a category
 * opens on its Basics (two to four controls, free), and everything deeper waits in sections a tap
 * drops open below them, several at once, so a user who wants "warmer" never meets an envelope
 * editor. ‹ › page through the categories without a back entry; ← and system back return to the
 * list at the scroll position it had, because [listState] lives above both.
 *
 * Stateless about what a control is: [sectionContent] draws a section's controls, so the panel
 * and the Compose test share this exact browser.
 */
@Composable
internal fun CustomizeBrowser(
    categories: List<CustomizeCategory>,
    sectionsOf: (CustomizeCategory) -> List<CustomizeSection>,
    nav: CustomizeNav,
    listState: LazyListState,
    openSections: Set<String>,
    onToggleSection: (CustomizeSection) -> Unit,
    sectionLocked: (CustomizeSection) -> Boolean,
    modifier: Modifier = Modifier,
    summaryOf: @Composable (CustomizeCategory) -> String = { it.summary },
    header: @Composable (CustomizeCategory?) -> Unit = {},
    sectionContent: @Composable (CustomizeSection) -> Unit,
) {
    val open = categories.firstOrNull { it.name == nav.openCategory }
    BackHandler(enabled = open != null) { nav.close() }
    Column(modifier) {
        header(open)
        if (open == null) {
            LazyColumn(
                state = listState,
                modifier = Modifier.weight(1f).fillMaxWidth(),
                contentPadding =
                    PaddingValues(start = GeodeSpacing.M, end = GeodeSpacing.M, top = GeodeSpacing.XS, bottom = GeodeSpacing.XL),
                verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
            ) {
                items(categories, key = { it.name }) { category ->
                    CategoryRow(category, summaryOf(category)) { nav.open(category.name) }
                }
            }
        } else {
            val order = categories.map { it.name }
            CategoryHeader(
                category = open,
                at = order.indexOf(open.name),
                count = order.size,
                previous = categories.getOrNull(order.indexOf(open.name) - 1),
                next = categories.getOrNull(order.indexOf(open.name) + 1),
                onBack = nav::close,
                onStep = { delta -> nav.step(order, delta) },
            )
            // One scroll per category, so › to the next starts at its top.
            key(open) {
                CategoryBody(
                    sections = sectionsOf(open),
                    openSections = openSections,
                    onToggleSection = onToggleSection,
                    sectionLocked = sectionLocked,
                    sectionContent = sectionContent,
                    modifier = Modifier.weight(1f).fillMaxWidth(),
                )
            }
        }
    }
}

private val ROW_HEIGHT = 64.dp
private const val HALF_TURN = 180f

@Composable
private fun CategoryRow(
    category: CustomizeCategory,
    summary: String,
    onOpen: () -> Unit,
) {
    val shape = RoundedCornerShape(GeodeRadius.Large)
    Row(
        Modifier
            .fillMaxWidth()
            .floatingGlass(shape = shape, elevation = 4.dp)
            .heightIn(min = ROW_HEIGHT)
            .clickable(role = Role.Button, onClickLabel = "Open", onClick = onOpen)
            .semantics(mergeDescendants = true) {}
            .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(category.icon, contentDescription = null, tint = accentTextColor())
        Column(Modifier.weight(1f).padding(start = GeodeSpacing.L)) {
            Text(category.title, style = MaterialTheme.typography.titleSmall)
            Text(
                summary,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun CategoryHeader(
    category: CustomizeCategory,
    at: Int,
    count: Int,
    previous: CustomizeCategory?,
    next: CustomizeCategory?,
    onBack: () -> Unit,
    onStep: (Int) -> Unit,
) {
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back to Customize")
        }
        Text(
            category.title,
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.weight(1f).semantics { heading() },
        )
        Text(
            "${at + 1} of $count",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        IconButton(onClick = { onStep(-1) }, enabled = previous != null) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, contentDescription = previous?.let { "Previous: ${it.title}" } ?: "Previous")
        }
        IconButton(onClick = { onStep(1) }, enabled = next != null) {
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = next?.let { "Next: ${it.title}" } ?: "Next")
        }
    }
}

@Composable
private fun CategoryBody(
    sections: List<CustomizeSection>,
    openSections: Set<String>,
    onToggleSection: (CustomizeSection) -> Unit,
    sectionLocked: (CustomizeSection) -> Boolean,
    sectionContent: @Composable (CustomizeSection) -> Unit,
    modifier: Modifier,
) {
    val state = rememberLazyListState()
    LazyColumn(
        state = state,
        modifier = modifier,
        contentPadding = PaddingValues(start = GeodeSpacing.L, end = GeodeSpacing.L, bottom = GeodeSpacing.XL),
    ) {
        items(sections, key = { it.path }) { section ->
            if (section.basics) {
                Column(Modifier.fillMaxWidth().padding(bottom = GeodeSpacing.S)) { sectionContent(section) }
            } else {
                SectionBlock(
                    section = section,
                    expanded = section.path in openSections,
                    locked = sectionLocked(section),
                    onToggle = { onToggleSection(section) },
                    content = { sectionContent(section) },
                )
            }
        }
    }
}

/** A section that drops open downwards: ▸ name (♛ when Pro and locked), its controls under it when open. */
@Composable
private fun SectionBlock(
    section: CustomizeSection,
    expanded: Boolean,
    locked: Boolean,
    onToggle: () -> Unit,
    content: @Composable () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val turn = if (expanded) 0f else -HALF_TURN / 2
    Column(Modifier.fillMaxWidth().animateContentSize(motion.defaultSpatialSpec())) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.Button, onClickLabel = if (expanded) "Close" else "Open", onClick = onToggle)
                .semantics(mergeDescendants = true) {
                    heading()
                    stateDescription = if (expanded) "Open" else "Closed"
                },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                Icons.Filled.ExpandMore,
                contentDescription = null,
                modifier = Modifier.rotate(turn),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                section.title,
                style = MaterialTheme.typography.titleSmall,
                color = accentTextColor(),
                modifier = Modifier.padding(start = 8.dp),
            )
            if (locked) ProCrown()
        }
        if (expanded) Column(Modifier.fillMaxWidth().padding(bottom = GeodeSpacing.S)) { content() }
    }
}
