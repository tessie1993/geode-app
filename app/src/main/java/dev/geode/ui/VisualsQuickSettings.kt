package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MultiChoiceSegmentedButtonRow
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
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
 * so the two can never disagree: Auto is [AutoDirectorStore] (as Settings › Behaviour ›
 * Auto-director), the timer, its sources and the playlist are [PlayerViewModel]'s Auto visuals
 * (`AutoVisualsPrefsStore`, as Settings › Auto visuals), and the glass rows are [GuiPrefs] through
 * [SettingsViewModel] (as Settings › Look). Gating stays where it is: a free user may switch Auto on
 * and gets the first minute of each song ([dev.geode.pro.AutoDirectorPolicy]); the timer's picks skip
 * locked styles by themselves.
 *
 * It behaves like an anchored dropdown from the top, the standard Android shape for a panel that
 * belongs to a spot on screen: a floating card under the status bar, Back or a tap anywhere outside
 * it dismisses it ([onDismiss]), and there is no scrim, so the visual stays fully lit around it. The
 * caller places this over the whole visual (it fills [modifier]'s bounds to catch the outside tap) and
 * animates it in and out.
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
        Column(
            Modifier
                .align(Alignment.TopCenter)
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))
                .padding(GeodeSpacing.EdgeInset)
                .widthIn(max = PANEL_MAX_WIDTH)
                .fillMaxWidth()
                // Drawn at the sheet's own opacity, so the slider below previews itself.
                // Dense glass of its own: the options slider tunes the sheet, and the sheet must not read through here.
                .floatingGlass(fillAlpha = DROP_IN_FILL)
                // Taps on the card's empty parts stay on the card instead of reaching the dismiss target.
                .pointerInput(Unit) { detectTapGestures { } }
                .verticalScroll(rememberScrollState())
                .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
            verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
        ) {
            QuickSettingsHeader(onDismiss)
            AutoRows(viewModel, onOpenPlaylist)
            GlassRows(gui, settings::setGuiPrefs)
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

@Composable
private fun AutoRows(
    viewModel: PlayerViewModel,
    onOpenPlaylist: (() -> Unit)?,
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

    LabeledSwitch(
        stringResource(R.string.visuals_qs_auto),
        checked = directorOn,
        onCheckedChange = director::setEnabled,
    )
    LabeledSwitch(
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
    QuickSlider(
        label = stringResource(R.string.visuals_qs_every),
        valueText = stringResource(R.string.visuals_qs_seconds, interval),
        value = interval.toFloat(),
        valueRange = INTERVAL_RANGE,
        onValueChange = {
            val seconds = it.roundToInt()
            if (source == TimerSource.PLAYLIST) viewModel.setVizPlaylistInterval(seconds) else viewModel.setRandomInterval(seconds)
        },
    )
    RowLabel(stringResource(R.string.visuals_qs_steps_through))
    CrystalSegmented(
        options = TimerSource.entries.map { stringResource(it.labelRes) },
        selected = source.ordinal,
        onSelect = { i ->
            val next = TimerSource.entries[i]
            picked = next
            // Switching while the timer runs moves it to the other mode; the controller stops the first.
            if (running && next != source) {
                if (next == TimerSource.PLAYLIST) viewModel.setVizPlaylistEnabled(true) else viewModel.setRandomEnabled(true)
            }
        },
    )
    if (source == TimerSource.RANDOM) {
        RandomPicks(viz, viewModel)
    } else {
        PlaylistRow(viz.vizPlaylist.size, onOpenPlaylist)
    }
}

/** What Random picks from: styles, saved presets and, where the engine is built in, MilkDrop presets. */
@Composable
private fun RandomPicks(
    viz: VizUiState,
    viewModel: PlayerViewModel,
) {
    val picks =
        buildList {
            add(Triple(R.string.autoviz_pick_styles, viz.randomIncludeStyles, viewModel::setRandomIncludeStyles))
            add(Triple(R.string.visuals_qs_pick_presets, viz.randomIncludePresets, viewModel::setRandomIncludePresets))
            if (MilkdropEngine.available) {
                add(Triple(R.string.visuals_qs_pick_milk, viz.randomIncludeMilk, viewModel::setRandomIncludeMilk))
            }
        }
    val cs = MaterialTheme.colorScheme
    MultiChoiceSegmentedButtonRow(
        Modifier
            .fillMaxWidth()
            .padding(top = GeodeSpacing.S),
    ) {
        picks.forEachIndexed { i, (labelRes, checked, onChange) ->
            SegmentedButton(
                checked = checked,
                onCheckedChange = onChange,
                shape = SegmentedButtonDefaults.itemShape(i, picks.size),
                modifier = Modifier.weight(1f),
                colors =
                    SegmentedButtonDefaults.colors(
                        activeContainerColor = cs.primaryContainer,
                        activeContentColor = cs.onPrimaryContainer,
                        inactiveContainerColor = Color.Transparent,
                        inactiveContentColor = cs.onSurface,
                        activeBorderColor = cs.outline,
                        inactiveBorderColor = cs.outline,
                    ),
            ) {
                Text(
                    stringResource(labelRes),
                    style = MaterialTheme.typography.labelMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
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
            .heightIn(min = GeodeSpacing.TouchTarget)
            .padding(top = GeodeSpacing.XS),
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
            style = MaterialTheme.typography.bodySmall,
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
    QuickSlider(
        label = stringResource(R.string.visuals_qs_sheet_opacity),
        valueText = stringResource(R.string.visuals_qs_percent, (gui.barOpacity * 100).roundToInt()),
        value = gui.barOpacity,
        valueRange = OPACITY_RANGE,
        onValueChange = { onGuiChange(gui.copy(barOpacity = it)) },
        modifier = Modifier.padding(top = GeodeSpacing.S),
    )
    val fade = gui.miniPlayerFadeSeconds.roundToInt()
    QuickSlider(
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

/** A compact two-line slider: the label and its value on one line, the slider under them. */
@Composable
private fun QuickSlider(
    label: String,
    valueText: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    steps: Int = 0,
) {
    Column(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Text(valueText, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        CrystalSlider(
            value = value.coerceIn(valueRange),
            onValueChange = onValueChange,
            valueRange = valueRange,
            steps = steps,
        )
    }
}

@Composable
private fun RowLabel(text: String) {
    Text(
        text,
        Modifier.padding(top = GeodeSpacing.XS, bottom = GeodeSpacing.XS),
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** About a phone's width: on a tablet or in landscape the card stays a dropdown, not a banner. */
private val PANEL_MAX_WIDTH = 420.dp

private val INTERVAL_RANGE =
    AutoVisualsPrefsStore.INTERVAL_SEC.first.toFloat()..AutoVisualsPrefsStore.INTERVAL_SEC.last.toFloat()

/** The range the floating glass reads the opacity in ([floatingGlass] clamps to it anyway). */
private val OPACITY_RANGE = GeodeGlass.MATTE_FILL_MIN..GeodeGlass.MATTE_FILL_MAX

/** How dense the drop-in panels are: the visual shows as colour, nothing behind reads as text. */
internal const val DROP_IN_FILL = 0.9f
