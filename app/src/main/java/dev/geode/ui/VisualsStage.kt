package dev.geode.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ripple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.WindowInsetsRulers
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.model.GestureTip
import dev.geode.model.TourSpot
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.Catalogue
import dev.geode.render.scene.SceneIds
import dev.geode.ui.cast.CastButton
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.tour.LocalTourNudge
import dev.geode.ui.tour.tourTarget
import kotlin.math.roundToInt

/**
 * The stage: the live visual, full screen, and the gestures on it ([visualGestures],
 * ui/VisualGestures.kt), each doing what Settings › Gestures & controls' map gives it
 * ([BehaviourPrefs.actionFor]). A swipe that starts in the system's gesture strips is left to
 * Android (back, home, the shade), and TalkBack gets every gesture as a custom action.
 *
 * [tapSplitPx] is where a tap stops counting as the top of the visual (quick settings) and starts
 * counting as the bottom (the mini player): the middle of what the options leave in view. [onPinch]
 * is Settings › Gestures & controls › Touch's pinch and twist (null while it is off).
 *
 * [remote] is given on a TV only ([stageRemote]): the stage then takes focus and the remote's keys.
 */
@Composable
internal fun BoxScope.VisualsStage(
    visualizerView: VisualizerView,
    behaviour: BehaviourPrefs,
    actions: VisualActions,
    recording: Boolean,
    tapSplitPx: () -> Float,
    onTapDown: (Long) -> Unit,
    onPinch: ((zoomFactor: Float, degrees: Float) -> Unit)?,
    remote: StageRemote? = null,
) {
    // The visual itself is not drawn here: the shell hosts the one VisualizerView beneath every
    // screen (VisualizerCanvasHost) and shows it through this transparent stage.
    val ring = remember { HoldRingState() }
    Box(
        Modifier
            .matchParentSize()
            .visualGestures(
                actionFor = behaviour::actionFor,
                ring = ring,
                tapSplitPx = tapSplitPx,
                holdArmed = !(recording && behaviour.actionFor(CanvasGesture.LONG_PRESS) == GestureAction.RECORD),
                onGesture = { gesture, step -> actions.run(behaviour.actionFor(gesture), step) },
                onTouchPoints = visualizerView.visualizerRenderer::submitTouchPoints,
                onPinch = onPinch,
                a11yActions = rememberVisualA11yActions(behaviour::actionFor, actions, recording),
                onTapDown = onTapDown,
            ).then(if (remote != null) Modifier.stageRemote(remote, behaviour, actions) else Modifier),
    )
    HoldRing(ring)
}

/**
 * The icons over the visual, the visible twins of its gestures: Cast, Settings, Studio and Full screen (the
 * options hidden or back) at the top right, ‹ › at the sides of the part of the visual in view,
 * Record at the bottom right just above the sheet. [endPx] moves the right-hand ones clear of the
 * landscape side panel, and [sheetTopPx] lifts Record over the portrait sheet; both are read in the
 * layout pass. ‹ › centre in what is left once both are taken off, so they never sit under the
 * sheet or the panel.
 *
 * [room] says which of them the options leave room for ([stageIconRoom]): an icon the sheet's top
 * edge or the panel would reach is faded out and takes no touch, like a sleeping one, so a tall
 * sheet never shows the icons through its glass or over its tabs.
 *
 * They fade with the shell's chrome through the layer's alpha, never AnimatedVisibility: the Cast
 * button feeds the cast mirror while Visuals is up, so it has to stay composed (CAST decision). A
 * faded icon takes no touches and no TalkBack focus, so a tap where it was reaches the visual.
 * Record stays while recording, red, as its stop, wherever the options leave it room.
 */
@Composable
internal fun BoxScope.StageIcons(
    chrome: ShellChromeState,
    sheetShown: Boolean,
    room: StageIconRoom,
    sheetTopPx: () -> Float,
    endPx: () -> Float,
    onFullScreen: () -> Unit,
    onOpenStudioRender: () -> Unit,
    onSettings: () -> Unit,
    onStep: (Int) -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val record by studioViewModel.recordState.collectAsStateWithLifecycle()
    val nudge = LocalTourNudge.current
    val glass = Modifier.floatingGlass(RoundedCornerShape(50), fillAlpha = GeodeGlass.MATTE_FILL)
    val fade = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
    val awake = chrome.awake
    val rowOn = awake && room.topRow
    val arrowsOn = awake && room.arrows
    val recordOn = (awake || record.recording) && room.record
    val shown = animateFloatAsState(if (rowOn) 1f else 0f, fade, label = "stage-icons")
    val arrowsShown = animateFloatAsState(if (arrowsOn) 1f else 0f, fade, label = "stage-arrows")
    val recordShown = animateFloatAsState(if (recordOn) 1f else 0f, fade, label = "stage-record")
    val toLeft = Modifier.offset { IntOffset(-endPx().roundToInt(), 0) }
    Row(
        Modifier
            .align(Alignment.TopEnd)
            .then(toLeft)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.End))
            .padding(GeodeSpacing.EdgeInset)
            .tourTarget(TourSpot.STAGE_TOOLS)
            .faded(shown)
            .then(glass),
    ) {
        // Cast stays composed through the fade (it feeds the cast mirror); while faded a cover takes
        // its taps and TalkBack skips it, as the other icons do when disabled.
        Box(Modifier.size(GeodeSpacing.TouchTarget), contentAlignment = Alignment.Center) {
            CastButton()
            if (!rowOn) {
                Box(
                    Modifier
                        .matchParentSize()
                        .clearAndSetSemantics {}
                        .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {},
                )
            }
        }
        // A connected display (HDMI, USB-C, a wireless display): only while one is there; filled while on.
        val display = rememberConnectedDisplayControl()
        display.display?.let { connected ->
            StageIconButton(
                StoneIcon.DISPLAY,
                stringResource(display.actionRes, connected.name),
                awake,
                display.toggle,
                selected = display.on,
            )
        }
        // Visuals › Settings, a page of the options (owner: an icon, no drop-down, no gesture).
        StageIconButton(StoneIcon.SETTINGS, stringResource(R.string.visuals_qs_title), rowOn, onSettings)
        // Export, loop render and the recordings live in Studio; this only takes you there.
        StageIconButton(StoneIcon.STUDIO, stringResource(R.string.visuals_open_render), rowOn, onOpenStudioRender)
        StageIconButton(
            StoneIcon.FULLSCREEN,
            // With the options hidden this is the visible way back to them, so it says so.
            stringResource(if (sheetShown) R.string.visuals_gesture_toggle_sheet else R.string.visuals_show_options),
            rowOn,
            onClick = {
                nudge(GestureTip.DOUBLE_TAP_SHEET)
                onFullScreen()
            },
        )
    }
    // ‹ › centre in the part of the visual in view: shrunk by the panel at the end (landscape) and
    // the sheet at the bottom (portrait), so Center* aligns to the visible area, not the whole stage.
    // They sit beside the side strips, not in them, so pressing one never starts Back and Back never
    // lands on one. A strip only matters where the visible part of the stage reaches it: with the
    // landscape panel open › sits mid-window, and in the expanded shell ‹ sits beside the rail, so a
    // fixed `windowInsetsPadding(safeGestures)` would push them in for nothing (and, being made of
    // insets the shell's `safeDrawingPadding` did not consume, by the wrong amount). The
    // `WindowInsetsRulers.SafeContent` rulers give each strip's inner edge in this layout's own
    // coordinates (Compose reference, `WindowInsetsRulers`; the edge-to-edge skill's rulers route),
    // so the visible area is cut only by the part of a strip that overlaps it. SafeContent, not
    // SafeGestures: in landscape the camera cutout sits mid-way down a side edge, exactly where an
    // arrow centres, and with three-button navigation no gesture strip covers it. Rulers are read in
    // the placement block, which is why the child is measured there (the reference's own sample).
    Box(
        Modifier
            .matchParentSize()
            .layout { measurable, constraints ->
                layout(constraints.maxWidth, constraints.maxHeight) {
                    val strips = WindowInsetsRulers.SafeContent.current
                    val end = endPx().roundToInt()
                    val ltr = layoutDirection == LayoutDirection.Ltr
                    // The visible part of the stage, in physical px: the panel is on the end side.
                    val visibleLeft = if (ltr) 0 else end
                    val visibleRight = constraints.maxWidth - if (ltr) end else 0
                    val left = (strips.left.current(0f).roundToInt() - visibleLeft).coerceAtLeast(0)
                    val right = (visibleRight - strips.right.current(constraints.maxWidth.toFloat()).roundToInt()).coerceAtLeast(0)
                    val width = (visibleRight - visibleLeft - left - right).coerceAtLeast(0)
                    val height = (constraints.maxHeight - sheetTopPx().roundToInt()).coerceAtLeast(0)
                    measurable.measure(Constraints.fixed(width, height)).place(visibleLeft + left, 0)
                }
            },
    ) {
        StageIconButton(
            StoneIcon.CHEVRON_LEFT,
            stringResource(R.string.visuals_previous_style),
            arrowsOn,
            {
                nudge(GestureTip.SWIPE_STYLES)
                onStep(-1)
            },
            Modifier
                .align(Alignment.CenterStart)
                .padding(GeodeSpacing.S)
                .faded(arrowsShown)
                .then(glass),
        )
        StageIconButton(
            StoneIcon.CHEVRON_RIGHT,
            stringResource(R.string.visuals_next_style),
            arrowsOn,
            {
                nudge(GestureTip.SWIPE_STYLES)
                onStep(1)
            },
            Modifier
                .align(Alignment.CenterEnd)
                .padding(GeodeSpacing.S)
                .tourTarget(TourSpot.STAGE_ARROWS)
                .faded(arrowsShown)
                .then(glass),
        )
    }
    StageIconButton(
        icon = if (record.recording) StoneIcon.STOP else StoneIcon.RECORD,
        description = stringResource(if (record.recording) R.string.visuals_stop_record else R.string.visuals_gesture_record),
        enabled = recordOn,
        onClick = {
            if (record.recording) {
                studioViewModel.stopRecording()
            } else {
                nudge(GestureTip.HOLD_TO_RECORD)
                studioViewModel.startRecording()
            }
        },
        modifier =
            Modifier
                .align(Alignment.BottomEnd)
                .offset { IntOffset(-endPx().roundToInt(), -sheetTopPx().roundToInt()) }
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.End))
                .padding(GeodeSpacing.EdgeInset)
                .tourTarget(TourSpot.STAGE_RECORD)
                .faded(recordShown)
                .then(glass),
        tint = if (record.recording) MaterialTheme.colorScheme.error else Color.Unspecified,
    )
}

/**
 * What the stage needs to take a TV remote: [stage] is its own focus, [options] the options'
 * (a focus group the remote enters), [optionsShown] whether they are up and [side] whether they
 * are the landscape panel at the end side rather than the sheet below.
 */
internal class StageRemote(
    val stage: FocusRequester,
    val options: FocusRequester,
    val optionsShown: Boolean,
    val side: Boolean,
)

/**
 * The stage as a focus target on a TV: the keys of [stageKeyGesture] run their gesture's action
 * from the map, first press only (a held key does not race through every style). The stage covers
 * the window, so spatial search can never find the options lying over it: while they are up the
 * arrow toward them (the end side for the panel, down for the sheet) is set to enter them, and
 * left still finds the rail beside the stage. Focused under the options it draws the accent's
 * hairline round the visual, so the remote's place is never invisible; full screen draws nothing.
 */
@Composable
private fun Modifier.stageRemote(
    remote: StageRemote,
    behaviour: BehaviourPrefs,
    actions: VisualActions,
): Modifier {
    var focused by remember { mutableStateOf(false) }
    val ring = MaterialTheme.colorScheme.primary
    return focusRequester(remote.stage)
        .focusProperties {
            if (remote.optionsShown) {
                if (remote.side) end = remote.options else down = remote.options
            }
        }.onFocusChanged { focused = it.isFocused }
        .onKeyEvent { event ->
            val key = event.nativeKeyEvent
            val gesture = stageKeyGesture(key.keyCode, remote.optionsShown)
            when {
                gesture == null -> false
                event.type != KeyEventType.KeyDown -> true
                else -> {
                    if (key.repeatCount == 0) actions.run(behaviour.actionFor(gesture.first), gesture.second)
                    true
                }
            }
        }.focusable()
        .then(if (focused && remote.optionsShown) Modifier.border(STAGE_FOCUS_RING, ring) else Modifier)
}

private val STAGE_FOCUS_RING = 2.dp

/** Which of the stage's icons the options leave room for: the top row, ‹ ›, and Record. */
internal data class StageIconRoom(
    val topRow: Boolean,
    val arrows: Boolean,
    val record: Boolean,
)

/**
 * The one rule for the stage icons and the options (owner: the icons overlapped the tall sheet's
 * tabs): an icon is drawn only where all of it lies in the part of the stage the options leave in
 * view, so nothing of the stage shows over (or through) the sheet or the panel, and only what is
 * drawn takes touches. All in px in the stage's own coordinates, from geometry alone, so every
 * height of the sheet, a drag between them and the panel at any width answer the same way.
 *
 * [sheetPx] is how far up from the stage's bottom the portrait sheet reaches (its glass, margin
 * and the dock under it; 0 beside the panel), [panelPx] how far in from the end side the landscape
 * panel reaches (its margin included; 0 in portrait). [topInsetPx] is the window's top inset the
 * row sits under, [iconPx] one icon's touch box, [marginPx] the row's and Record's margin and
 * [arrowMarginPx] the arrows'. The row is [TOP_ROW_ICONS] icons at the top end; Record sits at the
 * bottom end of the part in view and must stay below the row; ‹ › centre vertically in that part
 * and must stay below the row too (› shares the end side with it).
 */
internal fun stageIconRoom(
    stageWidthPx: Float,
    stageHeightPx: Float,
    sheetPx: Float,
    panelPx: Float,
    topInsetPx: Float,
    iconPx: Float,
    marginPx: Float,
    arrowMarginPx: Float,
): StageIconRoom {
    val openWidth = stageWidthPx - panelPx
    val openHeight = stageHeightPx - sheetPx
    val rowBottom = topInsetPx + marginPx + iconPx
    val rowWidth = TOP_ROW_ICONS * iconPx + 2 * marginPx
    val arrowBox = iconPx + 2 * arrowMarginPx
    return StageIconRoom(
        topRow = rowWidth <= openWidth && rowBottom <= openHeight,
        arrows = 2 * arrowBox <= openWidth && (openHeight - arrowBox) / 2 >= rowBottom,
        record = iconPx + 2 * marginPx <= openWidth && openHeight - marginPx - iconPx >= rowBottom,
    )
}

/** The top row's icons: Cast, Settings, Studio and Full screen. */
private const val TOP_ROW_ICONS = 4

/** The layer's alpha from [alpha], read in the draw pass, so a fade never recomposes the icons. */
private fun Modifier.faded(alpha: State<Float>): Modifier = graphicsLayer { this.alpha = alpha.value }

/**
 * A 48 dp icon target with a TAP haptic. Disabled ([enabled] false, while faded), it is no
 * clickable at all and out of TalkBack's reach, rather than a disabled button that would still
 * swallow the tap meant for the visual.
 */
@Composable
private fun StageIconButton(
    icon: StoneIcon,
    description: String,
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tint: Color = Color.Unspecified,
    selected: Boolean = false,
) {
    val view = LocalView.current
    val interaction = remember { MutableInteractionSource() }
    Box(
        modifier
            .size(GeodeSpacing.TouchTarget)
            .then(
                if (enabled) {
                    Modifier.clickable(
                        interactionSource = interaction,
                        indication = ripple(bounded = false, radius = GeodeSpacing.TouchTarget / 2),
                        role = Role.Button,
                        onClickLabel = null,
                    ) {
                        view.performStoneHaptic(StoneHapticCue.TAP)
                        onClick()
                    }
                } else {
                    Modifier.clearAndSetSemantics {}
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon.forState(selected), description, tint = if (tint == Color.Unspecified) LocalContentColor.current else tint)
    }
}

/**
 * What the visual's gestures can do here, for the gesture map to choose from ([VisualActions]).
 * Randomize everything is a new style, a preset for it and every Customize category rolled
 * (`randomizeEverything`, native core/viz/RandomizeAll). Everyone gets the style and preset among
 * what they may keep; without Pro the roll follows Customize's dice (native's `#PRO` lock): the free
 * Basics roll, the rest stays as the preset made it, and no paywall interrupts the gesture.
 */
@Composable
internal fun rememberStageActions(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    stepStyle: (Int) -> Unit,
    onShowMiniPlayer: () -> Unit,
    onQuickSettings: () -> Unit,
    onToggleSheet: () -> Unit,
    onMacros: () -> Unit,
    onRecord: () -> Unit,
): VisualActions {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val touchPulse = rememberTouchPulse(visualizerView.visualizerRenderer::submitTouchPoints)
    val gate = { viewModel.gate.value }
    return VisualActions(
        showMiniPlayer = onShowMiniPlayer,
        quickSettings = onQuickSettings,
        toggleSheet = onToggleSheet,
        stepStyle = stepStyle,
        stepPreset = { step -> stepPreset(viewModel, gate(), step) },
        // With fewer than two looks in the playlist, the styles step instead.
        stepPlaylist = { step -> if (!viewModel.stepVizPlaylist(step)) stepStyle(step) },
        macros = onMacros,
        record = onRecord,
        randomizeAll = { visualsViewModel.randomizeEverything(basicsOnly = !gate().allows(ProFeature.ADVANCED_CUSTOMIZATION)) },
        touchPulse = touchPulse,
        playPause = viewModel::togglePlayPause,
    )
}

/** The actions a sideways swipe, or its ‹ › twins, can step through. */
internal val STEP_ACTIONS = setOf(GestureAction.STEP_STYLE, GestureAction.STEP_PRESET, GestureAction.STEP_PLAYLIST)

/**
 * Every style the stage swipes through, in the order the Styles page lists them (the native
 * picker's families one after another). MilkDrop is left out: its presets are a library of their
 * own, picked from the MilkDrop page.
 */
internal fun stageStyleOrder(): List<String> = Catalogue.picker.values.flatten() - SceneIds.MILKDROP

/**
 * The style [step] places (+1 next, -1 previous) from [current] in [order], wrapping at the ends
 * and skipping [locked] ones: a swipe that opened the paywall would stop the browse dead. A style
 * outside [order] (MilkDrop) starts from the first. Null when nothing else is open to the user.
 */
internal fun nextStageStyle(
    order: List<String>,
    current: String,
    step: Int,
    locked: (String) -> Boolean,
): String? {
    if (order.isEmpty()) return null
    val here = order.indexOf(current)
    val start =
        when {
            here >= 0 -> here
            step > 0 -> -1
            else -> 0
        }
    for (i in 1..order.size) {
        val id = order[Math.floorMod(start + step * i, order.size)]
        if (id != current && !locked(id)) return id
    }
    return null
}
