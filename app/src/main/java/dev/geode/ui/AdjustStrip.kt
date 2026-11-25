package dev.geode.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Casino
import androidx.compose.material.icons.filled.MoreHoriz
import androidx.compose.material.icons.filled.OpenInFull
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.engine.bridge.CustomizeNative
import dev.geode.engine.bridge.ParamsNative
import dev.geode.geodeContainer
import dev.geode.pro.FreeTier
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.stage.StageView
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.edgeFade
import dev.geode.ui.theme.floatingGlass
import kotlin.math.roundToInt

/**
 * Customize as a low strip at the bottom of the visual (owner, 2026-11-25; reports/ui "Visuals layout
 * proposal"): the picture keeps most of the screen while a control is turned, where the options sheet
 * covers up to 60 % of it.
 *
 * WHY a strip and not the sheet: adjusting is done while watching the result, so the control has to be
 * small and the picture large. While it is up the options, the docked mini player and the tab bar fade
 * out under it (the caller's `hiddenUnderDropIn`, as for the Macros drawer), so the screen holds one
 * task. It stays until Done or Back; it never fades by itself, because the user asked for it.
 *
 * Two depths, the shallow one first, and it always opens on that one. The shallow view is the macro
 * bank's knobs ([MacrosTab], the same knobs as Customize › Quick knobs and the Macros drawer), which is
 * all most changes need. More turns the strip into one row of category chips over pages of four
 * controls ([ControlPager]); a swipe brings the next four. All settings ([onOpenFull]) opens the full
 * list in the options on the same category, so nothing the sheet offers is lost.
 *
 * Styles and Presets still open the options sheet; only Customize has a strip so far.
 *
 * Only the container is new: which categories and controls a style has, each control's words, value,
 * range and default, and whether this user may keep a change all come from native and [ProLock], as
 * in [CustomizePanel]. [onHeight] reports how much of the screen's bottom the strip takes, margins and
 * system bar included, so the stage icons clear it.
 */
@Composable
internal fun AdjustStrip(
    visible: Boolean,
    viewModel: PlayerViewModel,
    visualizerView: StageView,
    onDone: () -> Unit,
    onOpenFull: () -> Unit,
    onHeight: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    var more by rememberSaveable { mutableStateOf(false) }
    // Leaving puts the strip back on the knobs, so the next opening starts shallow.
    val done = {
        more = false
        onDone()
    }
    // Back retraces the strip's own steps first: the deeper view, then the strip.
    CloseOnBack(enabled = visible, onBack = { if (more) more = false else done() })
    val motion = LocalGeodeMotion.current
    Box(modifier) {
        AnimatedVisibility(
            visible = visible,
            modifier = Modifier.align(Alignment.BottomCenter),
            enter = slideInVertically(motion.defaultSpatialSpec()) { it } + fadeIn(motion.fastEffectsSpec()),
            exit = slideOutVertically(motion.fastSpatialSpec()) { it } + fadeOut(motion.fastEffectsSpec()),
        ) {
            AdjustStripPanel(
                viewModel = viewModel,
                visualizerView = visualizerView,
                more = more,
                onMore = { more = it },
                onDone = done,
                onOpenFull = {
                    more = false
                    onOpenFull()
                },
                onHeight = onHeight,
            )
        }
    }
}

@Composable
private fun AdjustStripPanel(
    viewModel: PlayerViewModel,
    visualizerView: StageView,
    more: Boolean,
    onMore: (Boolean) -> Unit,
    onDone: () -> Unit,
    onOpenFull: () -> Unit,
    onHeight: (Int) -> Unit,
) {
    val context = LocalContext.current
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val vizState = viewModel.vizState.collectAsStateWithLifecycle()
    val sceneId by remember { derivedStateOf { vizState.value.sceneId } }
    val unlocked = gate.allows(ProFeature.ADVANCED_CUSTOMIZATION)
    // The same gate as the Customize panel, which the strip sits outside of.
    val proLock =
        remember(unlocked, paywall) {
            ProLock(
                unlocked = unlocked,
                free = FreeTier.freeCustomizeControls(CustomizeMapTable.basicControls),
                onLocked = { paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION)) },
            )
        }
    val host = remember(viewModel, visualsViewModel, visualizerView) { AdvancedHost(viewModel, visualsViewModel, visualizerView) }
    val groups = remember { context.geodeContainer.customizeGroups }
    val remembered by groups.groups.collectAsStateWithLifecycle()
    val macroStore = remember { context.geodeContainer.macros }
    val macros by macroStore.state.collectAsStateWithLifecycle()
    val locked by visualsViewModel.lockedParams.collectAsStateWithLifecycle()
    val history by visualsViewModel.paramHistory.collectAsStateWithLifecycle()

    // What this style offers beyond the knobs, as native says; the knobs are the strip's first view.
    val shown = remember(sceneId) { CustomizeMapTable.shownOn(sceneId) }
    val categories =
        remember(shown) { CustomizeCategory.entries.filter { it.name in shown && it != CustomizeCategory.QUICK_KNOBS } }
    val category = categories.firstOrNull { it.name == remembered.tab } ?: categories.firstOrNull()
    val deep = more && category != null
    val title = stringResource(R.string.visuals_tab_customize)
    val openFull = {
        // The full list opens where the strip was: on the knobs, or on the category in view.
        val at = if (deep && category != null) category else CustomizeCategory.QUICK_KNOBS
        groups.selectTab(at.name)
        onOpenFull()
    }

    CompositionLocalProvider(
        LocalParamLocks provides (locked to visualsViewModel::toggleParamLock),
        LocalProLock provides proLock,
        LocalReactTo provides {
            // "Make it react to music" is the full list's Music tab, with its Reactions open.
            groups.expand("${CustomizeCategory.MUSIC.name}/reactions")
            groups.selectTab(CustomizeCategory.MUSIC.name)
            onOpenFull()
        },
    ) {
        ProvideKnobLinks(sceneId, proLock) {
            ParamSurface(sceneId = sceneId, query = "") {
                Column(
                    Modifier
                        // Measured here, outside the insets and the margin: what the strip takes off the screen's bottom.
                        .onSizeChanged { onHeight(it.height) }
                        .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal))
                        .padding(GeodeSpacing.EdgeInset)
                        .widthIn(max = SHEET_MAX_WIDTH)
                        .fillMaxWidth()
                        .floatingGlass(fillAlpha = DROP_IN_FILL)
                        // Taps on the strip's empty parts stay on it instead of reaching the stage's tap.
                        .pointerInput(Unit) { detectTapGestures { } }
                        // Drags that start here stay here: a knob's turn never steps the style.
                        .keepDragsInside()
                        .semantics { paneTitle = title }
                        .padding(horizontal = GeodeSpacing.S, vertical = GeodeSpacing.XS),
                ) {
                    StripTools(
                        title = title,
                        category = category?.title.orEmpty(),
                        deep = deep,
                        hasMore = categories.isNotEmpty(),
                        canUndo = history.canUndo,
                        canRoll = !deep || "${category?.name}:roll" in shown,
                        onBack = { onMore(false) },
                        onMore = { onMore(true) },
                        onUndo = visualsViewModel::undoParams,
                        onRoll = {
                            if (deep) {
                                val rolled = visualsViewModel.randomizeParams(category, basicsOnly = !proLock.unlocked)
                                if (!rolled && !proLock.unlocked) proLock.onLocked()
                            } else {
                                // The knobs' turns: free, and native rolls them (core/viz/MacroBank).
                                macroStore.rollValues(sceneId)
                            }
                        },
                        onOpenFull = openFull,
                        onDone = onDone,
                    )
                    if (deep && category != null) {
                        CategoryChips(categories, category) { groups.selectTab(it.name) }
                        // One pager per category, so a new category starts on its first page.
                        key(category.name) {
                            ControlPager(
                                sections = remember(category, shown) { CustomizeMapTable.sectionsOf(category).filter { it.path in shown } },
                                host = host,
                                onOpenSection = { path ->
                                    groups.expand(path)
                                    groups.selectTab(category.name)
                                    onOpenFull()
                                },
                            )
                        }
                    } else {
                        // One row of knobs shows; a bank with more scrolls inside the strip's height.
                        Column(Modifier.heightIn(max = KNOBS_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                            MacrosTab(macros, macroStore, sceneId, proLock, strip = true)
                        }
                    }
                }
            }
        }
    }
}

/**
 * The strip's one row of tools. Shallow: the title, More, Randomize, Undo, Done. Deep: Back, then All
 * settings, Randomize, Undo, Done; no title there, because the lit chip under the row names the
 * category and the row has no width to spare on a phone. Randomize, Undo and Done keep their places at
 * both depths; Undo is lit while there is something to undo, and Done is last, under the right thumb.
 */
@Composable
private fun StripTools(
    title: String,
    category: String,
    deep: Boolean,
    hasMore: Boolean,
    canUndo: Boolean,
    canRoll: Boolean,
    onBack: () -> Unit,
    onMore: () -> Unit,
    onUndo: () -> Unit,
    onRoll: () -> Unit,
    onOpenFull: () -> Unit,
    onDone: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        if (deep) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.adjust_back_to_knobs))
            }
            Spacer(Modifier.weight(1f))
            IconButton(onClick = onOpenFull) {
                Icon(Icons.Filled.OpenInFull, contentDescription = stringResource(R.string.adjust_all_settings))
            }
        } else {
            Text(
                title,
                modifier = Modifier.weight(1f).padding(start = GeodeSpacing.S).semantics { heading() },
                style = MaterialTheme.typography.titleMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (hasMore) TextButton(onClick = onMore) { Text(stringResource(R.string.adjust_more), maxLines = 1) }
        }
        IconButton(onClick = onRoll, enabled = canRoll) {
            Icon(
                Icons.Filled.Casino,
                contentDescription =
                    if (deep) {
                        stringResource(R.string.customize_a11y_randomize, category)
                    } else {
                        stringResource(R.string.macro_randomize_turns)
                    },
            )
        }
        IconButton(onClick = onUndo, enabled = canUndo) {
            Icon(
                Icons.AutoMirrored.Filled.Undo,
                contentDescription = stringResource(R.string.customize_a11y_undo),
                tint = if (canUndo) accentTextColor() else LocalContentColor.current,
            )
        }
        TextButton(onClick = onDone) { Text(stringResource(R.string.adjust_done), maxLines = 1) }
    }
}

/** The categories this style has, as one scrolling row of chips, the open one lit and scrolled into view. */
@Composable
private fun CategoryChips(
    categories: List<CustomizeCategory>,
    selected: CustomizeCategory,
    onSelect: (CustomizeCategory) -> Unit,
) {
    val state = rememberLazyListState()
    val at = categories.indexOf(selected)
    LaunchedEffect(at) { if (at >= 0) state.animateScrollToItem(at) }
    LazyRow(
        state = state,
        modifier = Modifier.fillMaxWidth().edgeFade(state, horizontal = true),
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items(categories, key = { it.name }) { category ->
            FilterChip(
                selected = category == selected,
                onClick = { onSelect(category) },
                label = { Text(category.title, maxLines = 1) },
            )
        }
    }
}

/**
 * A category's controls as pages of four, one row high: a swipe brings the next four (the knobs take
 * vertical drags, so the two never fight), and a row of dots says how many pages there are and which
 * is in view. The dots' line is kept on a one-page category too, so the strip does not change height
 * from chip to chip.
 *
 * The cells are built from the rows native hands over for each section (core/viz/CustomizeRows, the
 * same call [CustomizeControls] makes): a slider is a knob, a switch a switch, and anything that is
 * not one value (a palette, a pick from a list, the reactions) is a button that opens it in the full
 * list, where it has the room it needs.
 */
@Composable
private fun ControlPager(
    sections: List<CustomizeSection>,
    host: AdvancedHost,
    onOpenSection: (String) -> Unit,
) {
    val vizState = host.viewModel.vizState.collectAsStateWithLifecycle()
    val viz = vizState.value
    val liquidMode by host.visualsViewModel.liquidMode.collectAsStateWithLifecycle()
    val sceneId = LocalSceneId.current
    val transition = viz.transitionId != null
    val cells =
        remember(sections, sceneId, viz.params, transition, liquidMode) {
            sections.flatMap { section ->
                CustomizeNative
                    .customizeRows(sceneId, section.controls.joinToString("\n"), "", transition, liquidMode, viz.params.frame())
                    .lines()
                    .drop(1)
                    .filter { it.isNotEmpty() }
                    .mapNotNull { stripCell(it.split('|'), section.path) }
            }
        }
    val pages = remember(cells) { cells.chunked(CELLS_PER_PAGE) }
    val pager = rememberPagerState { pages.size }
    // Read when a move arrives, not while composing, so a cell recomposes only when its own row changes.
    val edit: (String, Float) -> Unit = { key, v -> host.visualsViewModel.editSceneParams(vizState.value.params.with(key, v)) }
    Column(Modifier.fillMaxWidth()) {
        HorizontalPager(state = pager, modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.Top) { page ->
            // At least a cell high, and taller when a large font needs it: nothing is clipped.
            Row(Modifier.fillMaxWidth().heightIn(min = CELL_HEIGHT)) {
                val row = pages.getOrNull(page).orEmpty()
                row.forEach { cell ->
                    key(cell.id) {
                        when (cell) {
                            is StripCell.Knob -> ParamKnob(cell, Modifier.weight(1f)) { edit(cell.paramKey, it) }
                            is StripCell.Toggle ->
                                ParamToggle(cell, Modifier.weight(1f)) { edit(cell.paramKey, if (it) 1f else 0f) }
                            is StripCell.Open -> OpenInList(cell, Modifier.weight(1f)) { onOpenSection(cell.section) }
                        }
                    }
                }
                repeat(CELLS_PER_PAGE - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
        PageDots(count = pages.size, current = { pager.currentPage })
    }
}

/** Which page of how many, as dots; TalkBack hears "2 of 7" when the page changes. */
@Composable
private fun PageDots(
    count: Int,
    current: () -> Int,
) {
    // Read here, so a new page redraws the dots and not the pager above them.
    val at = current()
    val spoken = if (count > 1) stringResource(R.string.adjust_page_of, at + 1, count) else ""
    val on = MaterialTheme.colorScheme.primary
    val off = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = DOT_OFF_ALPHA)
    Row(
        Modifier
            .fillMaxWidth()
            .height(DOTS_HEIGHT)
            .clearAndSetSemantics {
                if (spoken.isNotEmpty()) {
                    contentDescription = spoken
                    liveRegion = LiveRegionMode.Polite
                }
            },
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.XS, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (count > 1) {
            repeat(count) { page -> Box(Modifier.size(DOT_SIZE).clip(CircleShape).background(if (page == at) on else off)) }
        }
    }
}

/** One cell of a page: what native's row is, reduced to what the strip draws. */
@Immutable
private sealed interface StripCell {
    /** Unique in its category: the row's type and key. */
    val id: String
    val name: String

    /** The section's path, for opening it in the full list. */
    val section: String

    data class Knob(
        override val id: String,
        override val name: String,
        override val section: String,
        val lockKey: String,
        val paramKey: String,
        val value: Float,
        val low: Float,
        val high: Float,
        val default: Float?,
        val unit: Int,
        val whole: Boolean,
    ) : StripCell

    data class Toggle(
        override val id: String,
        override val name: String,
        override val section: String,
        val lockKey: String,
        val paramKey: String,
        val checked: Boolean,
    ) : StripCell

    data class Open(
        override val id: String,
        override val name: String,
        override val section: String,
    ) : StripCell
}

/** A control's shown name: a unit suffix in the key ("Particle life (s)") is dropped, as on the sliders. */
private fun shownName(text: String): String = text.removeSuffix(" (s)").removeSuffix(" (Hz)")

/**
 * A native row ([f], core/viz/CustomizeRows.hpp names its fields) as a strip cell; null for a row
 * that is words only (a hint, a note) or that this build cannot read, which the strip then leaves out
 * rather than fail on.
 */
private fun stripCell(
    f: List<String>,
    section: String,
): StripCell? =
    runCatching {
        when (f[0]) {
            "SLIDER" ->
                StripCell.Knob(
                    id = "SLIDER/${f[2]}",
                    name = shownName(f[3]),
                    section = section,
                    lockKey = f[1],
                    paramKey = f[2],
                    value = f[4].toFloat(),
                    low = f[5].toFloat(),
                    high = f[6].toFloat(),
                    default = f[7].toFloatOrNull(),
                    unit = f[8].toInt(),
                    whole = false,
                )
            "INT" ->
                StripCell.Knob(
                    id = "INT/${f[2]}",
                    name = shownName(f[3]),
                    section = section,
                    lockKey = f[1],
                    paramKey = f[2],
                    value = f[4].toFloat(),
                    low = f[5].toFloat(),
                    high = f[6].toFloat(),
                    default = null,
                    unit = ParamsNative.PARAM_UNIT_OWN,
                    whole = true,
                )
            "CHECK" -> StripCell.Toggle("CHECK/${f[2]}", shownName(f[3]), section, lockKey = f[1], paramKey = f[2], checked = f[4] == "1")
            "CHIPS" -> StripCell.Open("CHIPS/${f[2]}", shownName(f[1]), section)
            "PICK", "SHELL" -> StripCell.Open("${f[0]}/${f[2]}", shownName(f[3].ifEmpty { f[1] }), section)
            "PIECE" -> f[3].ifEmpty { f[4] }.takeIf { it.isNotEmpty() }?.let { StripCell.Open("PIECE/${f[1]}", it, section) }
            else -> null
        }
    }.getOrNull()

/**
 * One parameter as a knob, the macro knobs' size and gestures: drag up or down to turn it, double tap
 * to put it back. Its value is written inside the ring, and only while the knob is off its default
 * (a whole-number knob, which has none, always shows it), so a page reads as names and what was
 * changed. Its name under it is the button for the rest ([CellMenu]).
 *
 * A Pro control the user may not keep is try-then-pay, as on the sliders: dimmed and crowned, it still
 * turns and the picture follows while it is held; on release it springs back and the paywall offers it.
 */
@Composable
private fun ParamKnob(
    cell: StripCell.Knob,
    modifier: Modifier = Modifier,
    onChange: (Float) -> Unit,
) {
    val lock = LocalProLock.current
    val locked = !lock.allows(cell.lockKey)
    val span = cell.high - cell.low
    val shown =
        remember(cell.value, cell.default, cell.unit, cell.low, cell.high, cell.whole) {
            if (cell.whole) {
                cell.value.roundToInt().toString()
            } else {
                ParamsNative.paramText(cell.lockKey, cell.unit, cell.value, cell.low, cell.high, cell.default ?: Float.NaN)
            }
        }
    val offDefault = remember(cell.value, cell.default) { cell.default?.takeIf { ParamsNative.sliderOffDefault(cell.value, it) } }
    val reset: (() -> Unit)? = offDefault?.takeIf { !locked }?.let { d -> { onChange(d) } }
    val current by rememberUpdatedState(cell.value)
    val change by rememberUpdatedState(onChange)
    val latestReset by rememberUpdatedState(reset)
    val latestLock by rememberUpdatedState(lock)
    // Where the finger has the knob, kept apart from the value so a whole-number knob still turns smoothly.
    var position by remember { mutableFloatStateOf(cell.value) }
    // The value before a locked turn began, put back on release.
    var before by remember(cell.id) { mutableStateOf<Float?>(null) }
    // A locked turn cut off before its release (the cell leaves the page, its range changes) is put back too.
    DisposableEffect(cell.id) { onDispose { before?.let { change(it) } } }
    val travelPx = with(LocalDensity.current) { KNOB_TRAVEL.toPx() }
    val track = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = TRACK_ALPHA)
    val arc = MaterialTheme.colorScheme.primary
    val resetLabel = stringResource(R.string.customize_a11y_reset)
    val spoken = if (locked) stringResource(R.string.customize_a11y_part_of_pro, cell.name) else cell.name
    // The whole counts between the ends, so TalkBack and Switch Access step a whole-number knob one at a time.
    val steps = if (cell.whole) (span.roundToInt() - 1).coerceAtLeast(0) else 0
    Column(modifier.alpha(if (locked) LOCKED_KNOB_ALPHA else 1f), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            Modifier
                .size(GeodeSpacing.TouchTarget)
                .pointerInput(cell.id, locked, span, cell.low, cell.high, cell.whole) {
                    val release = {
                        if (locked) {
                            before?.let { change(it) }
                            before = null
                            latestLock.onLocked()
                        }
                    }
                    detectVerticalDragGestures(
                        onDragStart = {
                            position = current
                            if (locked) before = current
                        },
                        onDragEnd = { release() },
                        onDragCancel = { release() },
                    ) { input, dy ->
                        input.consume()
                        if (span > 0f) {
                            position = (position - dy / travelPx * span).coerceIn(cell.low, cell.high)
                            val next = if (cell.whole) position.roundToInt().toFloat() else position
                            if (next != current) change(next)
                        }
                    }
                }.pointerInput(cell.id) {
                    detectTapGestures(onDoubleTap = { latestReset?.invoke() })
                }.semantics {
                    contentDescription = spoken
                    stateDescription = shown
                    if (span > 0f) {
                        progressBarRangeInfo = ProgressBarRangeInfo(cell.value, cell.low..cell.high, steps)
                        setProgress { target ->
                            if (locked) {
                                latestLock.onLocked()
                            } else {
                                val to = target.coerceIn(cell.low, cell.high)
                                change(if (cell.whole) to.roundToInt().toFloat() else to)
                            }
                            true
                        }
                    }
                    latestReset?.let { action -> customActions = listOf(CustomAccessibilityAction(resetLabel) { true.also { action() } }) }
                },
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(GeodeSpacing.TouchTarget)) {
                val stroke = KNOB_STROKE.toPx()
                val arcSize = Size(size.width - stroke, size.height - stroke)
                val topLeft = Offset(stroke / 2f, stroke / 2f)
                val line = Stroke(stroke, cap = StrokeCap.Round)
                drawArc(track, KNOB_START_DEG, KNOB_SWEEP_DEG, false, topLeft, arcSize, style = line)
                val turned = if (span > 0f) ((cell.value - cell.low) / span).coerceIn(0f, 1f) else 0f
                drawArc(arc, KNOB_START_DEG, turned * KNOB_SWEEP_DEG, false, topLeft, arcSize, style = line)
            }
            if (cell.default == null || offDefault != null) {
                // The knob's own state description says the same to TalkBack.
                Text(
                    shown,
                    modifier = Modifier.padding(horizontal = KNOB_STROKE * 2).clearAndSetSemantics { },
                    style = MaterialTheme.typography.labelSmall,
                    color = accentTextColor(),
                    maxLines = 1,
                    overflow = TextOverflow.Clip,
                    softWrap = false,
                )
            }
        }
        CellMenu(cell.name, cell.lockKey, locked, reset)
    }
}

/** An on/off parameter as a switch; the whole cell top toggles it. Without Pro a locked one offers Pro instead. */
@Composable
private fun ParamToggle(
    cell: StripCell.Toggle,
    modifier: Modifier = Modifier,
    onChange: (Boolean) -> Unit,
) {
    val lock = LocalProLock.current
    val locked = !lock.allows(cell.lockKey)
    val spoken = if (locked) stringResource(R.string.customize_a11y_part_of_pro, cell.name) else cell.name
    Column(modifier.alpha(if (locked) LOCKED_KNOB_ALPHA else 1f), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .toggleable(value = cell.checked, role = Role.Switch) { if (locked) lock.onLocked() else onChange(it) }
                .semantics { contentDescription = spoken },
            contentAlignment = Alignment.Center,
        ) {
            Switch(checked = cell.checked, onCheckedChange = null)
        }
        CellMenu(cell.name, cell.lockKey, locked, onReset = null)
    }
}

/** A control that is more than one value: a button that opens its group in the full list. */
@Composable
private fun OpenInList(
    cell: StripCell.Open,
    modifier: Modifier = Modifier,
    onOpen: () -> Unit,
) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        FilledTonalIconButton(onClick = onOpen, modifier = Modifier.size(GeodeSpacing.TouchTarget)) {
            Icon(Icons.AutoMirrored.Filled.OpenInNew, contentDescription = stringResource(R.string.adjust_open_in_list, cell.name))
        }
        // The button already says the name with what it does.
        CellName(cell.name, Modifier.heightIn(min = GeodeSpacing.TouchTarget).clearAndSetSemantics { }, menu = false)
    }
}

/**
 * A cell's name as the button for its menu (Reset, Keep when randomizing, Make it react): a tap on the
 * name opens it, where the full list has a ⋮ a 80 dp cell has no room for. The ⋯ beside the name says
 * it opens something; the pin (kept when randomizing), the knob mark (a macro knob moves it) and the
 * crown (Pro) ride on the same line. A locked control has no menu, as in the list.
 */
@Composable
private fun CellMenu(
    name: String,
    lockKey: String,
    locked: Boolean,
    onReset: (() -> Unit)?,
) {
    val menuLabel = stringResource(R.string.customize_a11y_more_for, name)
    val marks: @Composable () -> Unit = {
        KeptMark(lockKey)
        KnobMark(rememberKnobSlot(lockKey))
        if (locked) ProCrown()
    }
    if (locked) {
        CellName(name, Modifier.heightIn(min = GeodeSpacing.TouchTarget), menu = false, marks = marks)
    } else {
        RowMenu(name, lockKey, onReset) { onOpen ->
            CellName(
                name,
                Modifier
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .then(
                        if (onOpen != null) {
                            Modifier.clickable(role = Role.Button, onClickLabel = menuLabel, onClick = onOpen)
                        } else {
                            Modifier
                        },
                    ),
                menu = onOpen != null,
                marks = marks,
            )
        }
    }
}

/** A cell's name: one line, two when the font is large, with its [marks] and, when it opens a menu, a ⋯. */
@Composable
private fun CellName(
    name: String,
    modifier: Modifier = Modifier,
    menu: Boolean,
    marks: @Composable () -> Unit = {},
) {
    Row(
        modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.XXS),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            name,
            modifier = Modifier.weight(1f, fill = false),
            style = MaterialTheme.typography.labelMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
        marks()
        if (menu) {
            Icon(
                Icons.Filled.MoreHoriz,
                contentDescription = null,
                modifier = Modifier.padding(start = GeodeSpacing.XXS).size(MENU_HINT_SIZE),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private const val CELLS_PER_PAGE = 4

/** A cell's least height: the 48 dp control over the 48 dp name row. */
private val CELL_HEIGHT = 96.dp

/** One row of the strip's knobs; a bank with more scrolls. */
private val KNOBS_MAX_HEIGHT = 136.dp

/** The finger travel that sweeps a knob from one end to the other: a comfortable thumb stroke. */
private val KNOB_TRAVEL = 200.dp
private val KNOB_STROKE = 4.dp
private const val KNOB_START_DEG = 135f
private const val KNOB_SWEEP_DEG = 270f

/** The unturned part of the ring: 3:1 against the glass, the least for a control's outline. */
private const val TRACK_ALPHA = 0.45f

/** How far a Pro knob the user may not keep is dimmed; the sliders' own value. */
private const val LOCKED_KNOB_ALPHA = 0.6f
private val DOTS_HEIGHT = 12.dp
private val DOT_SIZE = 6.dp
private const val DOT_OFF_ALPHA = 0.4f
private val MENU_HINT_SIZE = 14.dp
