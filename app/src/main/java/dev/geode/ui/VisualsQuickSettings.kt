package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.systemGestureExclusion
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChangeIgnoreConsumed
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.toggleableState
import androidx.compose.ui.state.ToggleableState
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.AutoDirectorStore
import dev.geode.data.GuiPrefs
import dev.geode.render.scene.MilkdropEngine
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass
import kotlin.math.roundToInt

/**
 * Visuals quick settings: the panel a tap on the top of the full-screen visual opens (ui-flow
 * decisions, Round 2). It holds the settings a listener reaches for while watching, split from
 * Customize by kind: what changes the look by itself (Auto, the style timer and what it steps
 * through, the visual playlist) and how the glass over the visual behaves (the sheet's opacity, how
 * long the mini player stays).
 *
 * It owns no state of its own. Every row reads and writes the store the matching Settings row uses,
 * so the two can never disagree: Auto is [AutoDirectorStore], the timer, its sources and the
 * playlist are [PlayerViewModel]'s Auto visuals (`AutoVisualsPrefsStore`), Auto's only home now
 * (Settings has no Auto group), and the glass rows are [GuiPrefs] through [SettingsViewModel] (as
 * Settings › Look). Gating stays where it is: a free user may switch Auto on
 * and gets the first minute of each song ([dev.geode.pro.AutoDirectorPolicy]); the timer's picks skip
 * locked styles by themselves.
 *
 * It behaves like an anchored dropdown from the top, the standard Android shape for a panel that
 * belongs to a spot on screen: a floating card under the status bar, Back or a tap anywhere outside
 * it dismisses it ([onDismiss]), and there is no scrim, so the visual stays fully lit around it. The
 * caller places this over the whole visual (it fills [modifier]'s bounds to catch the outside tap),
 * animates it in and out, and hides the options sheet while it is up ([hiddenUnderDropIn]), so only
 * the visual shows through the glass (UI V2 rule 6).
 *
 * It is dense on purpose (QA on #284: seven controls took a whole portrait screen and four rows of a
 * landscape one). Every control is one row of [ROW_HEIGHT]: a switch row toggles as a whole, a slider
 * row has its label and value on one line beside a slim M3 slider, and a pick is one row whose value
 * opens a menu (UI V2 rule 2, no segmented walls). From [TWO_COLUMN_WIDTH] the rows split into two
 * columns, the look's timer on the left and the rest on the right, so a landscape phone shows them
 * all without scrolling. The card stops [DOCK_CLEARANCE] above the bottom so it never runs under the
 * floating mini player, which is drawn over Visuals and takes a swipe up as "open Player".
 *
 * The card keeps its own drags ([keepDragsInside]): a fling that runs off the end of its scroll, or a
 * drag across its empty glass, never reaches anything behind or around it.
 *
 * The timer runs one of two modes that the controller keeps apart (starting one stops the other):
 * Random, which picks from styles, presets and MilkDrop presets, or the visual playlist in order. The
 * panel shows them as one switch and a "steps through" choice, which is how a listener thinks of it;
 * while neither runs, the choice is remembered locally so the switch starts the mode last picked.
 *
 * [onOpenPlaylist], when the shell has a place to edit the playlist, turns the playlist row into its
 * entry point; without it the row only shows how many looks the playlist holds.
 */
@Composable
internal fun VisualsQuickSettings(
    viewModel: PlayerViewModel,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    onOpenPlaylist: (() -> Unit)? = null,
) {
    val settings: SettingsViewModel = geodeViewModel()
    val gui by settings.guiPrefs.collectAsStateWithLifecycle()
    BackHandler(onBack = onDismiss)
    Box(modifier.fillMaxSize()) {
        // The outside tap: a bare hit target under the card, with no semantics of its own; TalkBack
        // users close the panel with its close button or Back.
        Box(
            Modifier
                .fillMaxSize()
                .pointerInput(onDismiss) { detectTapGestures { onDismiss() } },
        )
        BoxWithConstraints(
            Modifier
                .align(Alignment.TopCenter)
                .windowInsetsPadding(WindowInsets.safeDrawing)
                .padding(GeodeSpacing.EdgeInset)
                .padding(bottom = DOCK_CLEARANCE)
                .widthIn(max = PANEL_MAX_WIDTH)
                .fillMaxWidth(),
        ) {
            val twoColumns = maxWidth >= TWO_COLUMN_WIDTH
            Column(
                Modifier
                    .fillMaxWidth()
                    // Dense glass of its own: the options slider tunes the sheet, and the sheet must not read through here.
                    .floatingGlass(fillAlpha = DROP_IN_FILL)
                    // Taps on the card's empty parts stay on the card instead of reaching the dismiss target.
                    .pointerInput(Unit) { detectTapGestures { } }
                    .keepDragsInside()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.XS),
            ) {
                QuickSettingsHeader(onDismiss)
                QuickSettingsRows(viewModel, gui, settings::setGuiPrefs, onOpenPlaylist, twoColumns)
            }
        }
    }
}

/**
 * Hides the options under an open drop-in (quick settings or the Macros drawer): the sheet or the
 * landscape panel fades to [fade] (the caller animates it on the motion scheme's effects spec, so
 * "Remove animations" makes it a cut), and while [open] it takes no touch or keyboard focus and
 * leaves the TalkBack tree. Its size and the nav state are untouched, so it comes back exactly where it was.
 */
@OptIn(ExperimentalComposeUiApi::class)
internal fun Modifier.hiddenUnderDropIn(
    open: Boolean,
    fade: () -> Float,
): Modifier =
    graphicsLayer { alpha = fade() }
        .then(
            if (open) {
                Modifier
                    .clearAndSetSemantics { }
                    // Keyboard focus moving on from the drop-in skips the hidden pages too.
                    .focusProperties { onEnter = { cancelFocusChange() } }
                    .focusGroup()
                    .pointerInput(Unit) { swallowEverything() }
            } else {
                Modifier
            },
        )

/**
 * A drop-in panel keeps every drag that starts on it. Its own controls and scroll take theirs first;
 * a drag left over (across the glass, or a fling past the end of the scroll) is consumed here, so the
 * stage's gestures, the mini player and the shell never see it.
 */
internal fun Modifier.keepDragsInside(): Modifier =
    nestedScroll(KeepScrollInside)
        .pointerInput(Unit) { claimLeftoverDrags() }

/** Takes whatever scroll and fling the panel's own scroll leaves, so none of it travels outward. */
private object KeepScrollInside : NestedScrollConnection {
    override fun onPostScroll(
        consumed: Offset,
        available: Offset,
        source: NestedScrollSource,
    ): Offset = available

    override suspend fun onPostFling(
        consumed: Velocity,
        available: Velocity,
    ): Velocity = available
}

/**
 * Consumes a gesture's moves once it has travelled past twice the touch slop. The children got the
 * events first (this sits outside them), and every M3 control and the scroll claim their drag at one
 * slop, so they are never starved; what reaches this point unclaimed would otherwise leak outward.
 */
private suspend fun PointerInputScope.claimLeftoverDrags() {
    awaitEachGesture {
        awaitFirstDown(requireUnconsumed = false)
        val claimAt = viewConfiguration.touchSlop * CLAIM_SLOP_FACTOR
        var travel = 0f
        var claimed = false
        do {
            val event = awaitPointerEvent()
            event.changes.forEach { change ->
                if (!claimed) {
                    travel += change.positionChangeIgnoreConsumed().getDistance()
                    claimed = travel > claimAt
                }
                if (claimed && !change.isConsumed) change.consume()
            }
        } while (event.changes.any { it.pressed })
    }
}

/** Every touch on a hidden sheet ends here, before its pages see it. */
private suspend fun PointerInputScope.swallowEverything() {
    awaitPointerEventScope {
        while (true) {
            awaitPointerEvent(PointerEventPass.Initial).changes.forEach { it.consume() }
        }
    }
}

/** Which of the controller's two timed modes the timer switch runs. */
private enum class TimerSource(
    @param:StringRes val labelRes: Int,
) {
    RANDOM(R.string.visuals_qs_source_random),
    PLAYLIST(R.string.visuals_qs_source_playlist),
}

/** One of Random's sources: its label, whether it is in, and how to change that. */
private class RandomPick(
    @param:StringRes val labelRes: Int,
    val checked: Boolean,
    val onChange: (Boolean) -> Unit,
)

@Composable
private fun QuickSettingsHeader(onDismiss: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            stringResource(R.string.visuals_qs_title),
            Modifier.weight(1f),
            style = MaterialTheme.typography.titleMedium,
        )
        IconButton(onClick = onDismiss) {
            StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.action_close))
        }
    }
}

/**
 * The seven controls. In one column they run top to bottom; in two, the timer (Auto, the timer
 * switch, its interval and what it steps through) is on the left and its source, then the glass, on
 * the right.
 */
@Composable
private fun QuickSettingsRows(
    viewModel: PlayerViewModel,
    gui: GuiPrefs,
    onGuiChange: (GuiPrefs) -> Unit,
    onOpenPlaylist: (() -> Unit)?,
    twoColumns: Boolean,
) {
    val context = LocalContext.current
    val director = remember { AutoDirectorStore.get(context) }
    val directorOn by director.enabled.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    var picked by rememberSaveable { mutableStateOf(if (viz.vizPlaylistEnabled) TimerSource.PLAYLIST else TimerSource.RANDOM) }
    // A mode started elsewhere (Settings, the Now Playing Auto button) wins over the local pick.
    val source =
        when {
            viz.vizPlaylistEnabled -> TimerSource.PLAYLIST
            viz.randomEnabled -> TimerSource.RANDOM
            else -> picked
        }
    val running = viz.randomEnabled || viz.vizPlaylistEnabled
    val timer: @Composable () -> Unit = {
        SwitchRow(stringResource(R.string.visuals_qs_auto), checked = directorOn, onCheckedChange = director::setEnabled)
        SwitchRow(
            stringResource(R.string.visuals_qs_timer),
            checked = running,
            onCheckedChange = { on ->
                when {
                    on && source == TimerSource.PLAYLIST -> viewModel.setVizPlaylistEnabled(true)
                    on -> viewModel.setRandomEnabled(true)
                    viz.vizPlaylistEnabled -> viewModel.setVizPlaylistEnabled(false)
                    else -> viewModel.setRandomEnabled(false)
                }
            },
        )
        val interval = if (source == TimerSource.PLAYLIST) viz.vizPlaylistIntervalSec else viz.randomIntervalSec
        SliderRow(
            label = stringResource(R.string.visuals_qs_every),
            valueText = stringResource(R.string.visuals_qs_seconds, interval),
            value = interval.toFloat(),
            valueRange = INTERVAL_RANGE,
            onValueChange = {
                val seconds = it.roundToInt()
                if (source == TimerSource.PLAYLIST) viewModel.setVizPlaylistInterval(seconds) else viewModel.setRandomInterval(seconds)
            },
        )
        SourceRow(source) { next ->
            picked = next
            // Switching while the timer runs moves it to the other mode; the controller stops the first.
            if (running && next != source) {
                if (next == TimerSource.PLAYLIST) viewModel.setVizPlaylistEnabled(true) else viewModel.setRandomEnabled(true)
            }
        }
    }
    val rest: @Composable () -> Unit = {
        if (source == TimerSource.RANDOM) {
            RandomPicksRow(viz, viewModel)
        } else {
            PlaylistRow(viz.vizPlaylist.size, onOpenPlaylist)
        }
        GlassRows(gui, onGuiChange)
    }
    if (twoColumns) {
        Row(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.L)) {
            Column(Modifier.weight(1f)) { timer() }
            Column(Modifier.weight(1f)) { rest() }
        }
    } else {
        timer()
        rest()
    }
}

/** "Steps through": Random or the visual playlist, one row whose value opens the choice. */
@Composable
private fun SourceRow(
    source: TimerSource,
    onSelect: (TimerSource) -> Unit,
) {
    MenuRow(
        label = stringResource(R.string.visuals_qs_steps_through),
        value = stringResource(source.labelRes),
    ) { close ->
        TimerSource.entries.forEach { option ->
            val isSelected = option == source
            DropdownMenuItem(
                text = { Text(stringResource(option.labelRes)) },
                onClick = {
                    onSelect(option)
                    close()
                },
                leadingIcon = { CheckSlot(isSelected) },
                modifier = Modifier.semantics { selected = isSelected },
            )
        }
    }
}

/**
 * What Random picks from: styles, saved presets and, where the engine is built in, MilkDrop presets.
 * One row with the picks as its value; the menu stays open while they are ticked.
 */
@Composable
private fun RandomPicksRow(
    viz: VizUiState,
    viewModel: PlayerViewModel,
) {
    val picks =
        buildList {
            add(RandomPick(R.string.autoviz_pick_styles, viz.randomIncludeStyles, viewModel::setRandomIncludeStyles))
            add(RandomPick(R.string.visuals_qs_pick_presets, viz.randomIncludePresets, viewModel::setRandomIncludePresets))
            if (MilkdropEngine.available) {
                add(RandomPick(R.string.visuals_qs_pick_milk, viz.randomIncludeMilk, viewModel::setRandomIncludeMilk))
            }
        }
    val names = picks.filter { it.checked }.map { stringResource(it.labelRes) }
    MenuRow(
        label = stringResource(R.string.autoviz_pick_from),
        value = if (names.isEmpty()) stringResource(R.string.music_moment_none) else names.joinToString(", "),
    ) {
        picks.forEach { pick ->
            DropdownMenuItem(
                text = { Text(stringResource(pick.labelRes)) },
                onClick = { pick.onChange(!pick.checked) },
                leadingIcon = { CheckSlot(pick.checked) },
                modifier =
                    Modifier.semantics {
                        role = Role.Checkbox
                        toggleableState = ToggleableState(pick.checked)
                    },
            )
        }
    }
}

/**
 * The visual playlist's row: how many looks it holds and, when [onOpen] is given, the way in to
 * editing it.
 */
@Composable
private fun PlaylistRow(
    count: Int,
    onOpen: (() -> Unit)?,
) {
    val rowModifier =
        if (onOpen != null) {
            Modifier.clickable(role = Role.Button, onClick = onOpen)
        } else {
            Modifier
        }
    Row(
        rowModifier
            .fillMaxWidth()
            .heightIn(min = ROW_HEIGHT),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(stringResource(R.string.visuals_qs_playlist), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Text(
            if (count == 0) {
                stringResource(R.string.visuals_qs_playlist_empty)
            } else {
                pluralStringResource(R.plurals.visuals_qs_playlist_count, count, count)
            },
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (onOpen != null) {
            StoneIconArt(StoneIcon.QUEUE, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** The glass over the visual: the sheet's opacity and the mini player's fade. */
@Composable
private fun GlassRows(
    gui: GuiPrefs,
    onGuiChange: (GuiPrefs) -> Unit,
) {
    SliderRow(
        label = stringResource(R.string.visuals_qs_sheet_opacity),
        valueText = stringResource(R.string.visuals_qs_percent, (gui.barOpacity * 100).roundToInt()),
        value = gui.barOpacity,
        valueRange = OPACITY_RANGE,
        onValueChange = { onGuiChange(gui.copy(barOpacity = it)) },
    )
    val fade = gui.miniPlayerFadeSeconds.roundToInt()
    SliderRow(
        label = stringResource(R.string.visuals_qs_fade),
        valueText =
            if (fade == 0) {
                stringResource(R.string.visuals_qs_fade_never)
            } else {
                stringResource(R.string.visuals_qs_seconds, fade)
            },
        value = fade.toFloat(),
        valueRange = 0f..GuiPrefs.MINI_PLAYER_FADE_MAX_SECONDS,
        // Whole seconds: 0 (never), 1 … 10.
        steps = GuiPrefs.MINI_PLAYER_FADE_MAX_SECONDS.roundToInt() - 1,
        onValueChange = { onGuiChange(gui.copy(miniPlayerFadeSeconds = it.roundToInt().toFloat())) },
    )
}

/**
 * A switch and its label as one control: the whole row toggles, and TalkBack reads the label with
 * the state ("Auto, on, switch"); the M3 switch itself takes no touch of its own.
 */
@Composable
private fun SwitchRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = ROW_HEIGHT)
            .toggleable(value = checked, role = Role.Switch, onValueChange = onCheckedChange),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
        Switch(checked = checked, onCheckedChange = null)
    }
}

/**
 * One line: the label, its value, then a slim M3 slider. The slider carries the label and the value
 * text for TalkBack, which otherwise reads only a bare percentage.
 */
@Composable
private fun SliderRow(
    label: String,
    valueText: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit,
    steps: Int = 0,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = ROW_HEIGHT),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(
            label,
            Modifier.weight(LABEL_WEIGHT),
            style = MaterialTheme.typography.bodyMedium,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            valueText,
            Modifier.widthIn(min = VALUE_MIN_WIDTH),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.End,
            maxLines = 1,
        )
        SlimSlider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            steps = steps,
            modifier =
                Modifier
                    .weight(SLIDER_WEIGHT)
                    .semantics {
                        contentDescription = label
                        stateDescription = valueText
                    },
        )
    }
}

/**
 * Material's slider with a thinner track and a shorter handle, so it sits in a [ROW_HEIGHT] row;
 * its touch area stays the slider's full width and row height. A thumb at 0 sits in the back-gesture
 * strip, so the slider is excluded from it (as Crystal's slider was).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SlimSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int,
    modifier: Modifier = Modifier,
) {
    val interaction = remember { MutableInteractionSource() }
    val colors = SliderDefaults.colors()
    Slider(
        value = value.coerceIn(valueRange),
        onValueChange = onValueChange,
        modifier = modifier.systemGestureExclusion(),
        colors = colors,
        interactionSource = interaction,
        steps = steps,
        thumb = { SliderDefaults.Thumb(interactionSource = interaction, colors = colors, thumbSize = SLIM_THUMB) },
        track = { state -> SliderDefaults.Track(sliderState = state, modifier = Modifier.height(SLIM_TRACK), colors = colors) },
        valueRange = valueRange,
    )
}

/**
 * A pick as one row (UI V2 rule 2): the label, the current value and a ▾; a tap opens [menu] under
 * it, which gets a way to close itself.
 */
@Composable
private fun MenuRow(
    label: String,
    value: String,
    menu: @Composable (close: () -> Unit) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = ROW_HEIGHT)
                .clickable(role = Role.DropdownList) { open = true },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
        ) {
            Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(
                value,
                Modifier.widthIn(max = MENU_VALUE_MAX_WIDTH),
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.primary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            StoneIconArt(StoneIcon.EXPAND, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            menu { open = false }
        }
    }
}

/** A menu item's leading tick, or the same room empty, so the labels line up. */
@Composable
private fun CheckSlot(checked: Boolean) {
    if (checked) {
        StoneIconArt(StoneIcon.CHECK, null)
    } else {
        Box(Modifier.size(CHECK_SLOT))
    }
}

/**
 * The card's widest: a phone's width in portrait, and wide enough on a landscape phone or a tablet
 * for the two columns, while it stays a dropdown rather than a banner.
 */
private val PANEL_MAX_WIDTH = 720.dp

/** From this card width the rows go into two columns. */
private val TWO_COLUMN_WIDTH = 600.dp

/**
 * One control per row. Below the 48 dp minimum on purpose (QA asked for 40–44 dp): the switch and
 * menu rows are full-width targets, and Compose widens a touch target under 48 dp to 48 dp where no
 * neighbour claims the touch.
 */
private val ROW_HEIGHT = 44.dp

/** The floating mini player's 64 dp and its margin: the card ends above it. */
private val DOCK_CLEARANCE = 76.dp

private val SLIM_TRACK = 6.dp
private val SLIM_THUMB = DpSize(4.dp, 24.dp)
private val VALUE_MIN_WIDTH = 40.dp
private val MENU_VALUE_MAX_WIDTH = 160.dp
private val CHECK_SLOT = 24.dp
private const val LABEL_WEIGHT = 1f
private const val SLIDER_WEIGHT = 1.2f
private const val CLAIM_SLOP_FACTOR = 2f

private val INTERVAL_RANGE =
    AutoVisualsPrefsStore.INTERVAL_SEC.first.toFloat()..AutoVisualsPrefsStore.INTERVAL_SEC.last.toFloat()

/** The range the floating glass reads the opacity in ([floatingGlass] clamps to it anyway). */
private val OPACITY_RANGE = GeodeGlass.MATTE_FILL_MIN..GeodeGlass.MATTE_FILL_MAX

/** How dense the drop-in panels are: the visual shows as colour, nothing behind reads as text. */
internal const val DROP_IN_FILL = 0.9f
