package dev.geode.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.clickable
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
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.WindowInsetsRulers
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.LayoutDirection
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.cast.CastButton
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.tour.GestureTip
import dev.geode.ui.tour.GestureTips
import kotlin.math.roundToInt

/**
 * The stage: the live visual, full screen, and the gestures on it ([visualGestures],
 * ui/VisualGestures.kt), each doing what Settings › Behaviour's gesture map gives it
 * ([BehaviourPrefs.actionFor]). A swipe that starts in the system's gesture strips is left to
 * Android (back, home, the shade), and TalkBack gets every gesture as a custom action.
 *
 * [tapSplitPx] is where a tap stops counting as the top of the visual (quick settings) and starts
 * counting as the bottom (the mini player): the middle of what the options leave in view.
 */
@Composable
internal fun BoxScope.VisualsStage(
    visualizerView: VisualizerView,
    behaviour: BehaviourPrefs,
    actions: VisualActions,
    recording: Boolean,
    tapSplitPx: () -> Float,
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
                a11yActions = rememberVisualA11yActions(behaviour::actionFor, actions, recording),
            ),
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
 * They fade with the shell's chrome through the layer's alpha, never AnimatedVisibility: the Cast
 * button feeds the cast mirror while Visuals is up, so it has to stay composed (CAST decision). A
 * faded icon takes no touches and no TalkBack focus, so a tap where it was reaches the visual.
 * Record stays while a take is being recorded, red, as its stop.
 */
@Composable
internal fun BoxScope.StageIcons(
    chrome: ShellChromeState,
    sheetShown: Boolean,
    sheetTopPx: () -> Float,
    endPx: () -> Float,
    onFullScreen: () -> Unit,
    onOpenStudioRender: () -> Unit,
    onSettings: () -> Unit,
    onStep: (Int) -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val glass = Modifier.floatingGlass(RoundedCornerShape(50), fillAlpha = GeodeGlass.MATTE_FILL)
    val fade = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
    val awake = chrome.awake
    val shown = animateFloatAsState(if (awake) 1f else 0f, fade, label = "stage-icons")
    val recordShown = animateFloatAsState(if (awake || takes.recording) 1f else 0f, fade, label = "stage-record")
    val toLeft = Modifier.offset { IntOffset(-endPx().roundToInt(), 0) }
    Row(
        Modifier
            .align(Alignment.TopEnd)
            .then(toLeft)
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.End))
            .padding(GeodeSpacing.EdgeInset)
            .faded(shown)
            .then(glass),
    ) {
        // Cast stays composed through the fade (it feeds the cast mirror); while faded a cover takes
        // its taps and TalkBack skips it, as the other icons do when disabled.
        Box(Modifier.size(GeodeSpacing.TouchTarget), contentAlignment = Alignment.Center) {
            CastButton()
            if (!awake) {
                Box(
                    Modifier
                        .matchParentSize()
                        .clearAndSetSemantics {}
                        .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {},
                )
            }
        }
        // Visuals › Settings, a page of the options (owner: an icon, no drop-down, no gesture).
        StageIconButton(StoneIcon.SETTINGS, stringResource(R.string.visuals_qs_title), awake, onSettings)
        // Export, loop render and the takes list live in Studio; this only takes you there.
        StageIconButton(StoneIcon.STUDIO, stringResource(R.string.visuals_open_render), awake, onOpenStudioRender)
        StageIconButton(
            StoneIcon.FULLSCREEN,
            // With the options hidden this is the visible way back to them, so it says so.
            stringResource(if (sheetShown) R.string.visuals_gesture_toggle_sheet else R.string.visuals_show_options),
            awake,
            onClick = {
                GestureTips.nudge(GestureTip.DOUBLE_TAP_SHEET)
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
    // `WindowInsetsRulers.SafeGestures` rulers give each strip's inner edge in this layout's own
    // coordinates (Compose reference, `WindowInsetsRulers`; the edge-to-edge skill's rulers route),
    // so the visible area is cut only by the part of a strip that overlaps it. Rulers are read in
    // the placement block, which is why the child is measured there (the reference's own sample).
    Box(
        Modifier
            .matchParentSize()
            .layout { measurable, constraints ->
                layout(constraints.maxWidth, constraints.maxHeight) {
                    val strips = WindowInsetsRulers.SafeGestures.current
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
            awake,
            {
                GestureTips.nudge(GestureTip.SWIPE_STYLES)
                onStep(-1)
            },
            Modifier
                .align(Alignment.CenterStart)
                .padding(GeodeSpacing.S)
                .faded(shown)
                .then(glass),
        )
        StageIconButton(
            StoneIcon.CHEVRON_RIGHT,
            stringResource(R.string.visuals_next_style),
            awake,
            {
                GestureTips.nudge(GestureTip.SWIPE_STYLES)
                onStep(1)
            },
            Modifier
                .align(Alignment.CenterEnd)
                .padding(GeodeSpacing.S)
                .faded(shown)
                .then(glass),
        )
    }
    StageIconButton(
        icon = if (takes.recording) StoneIcon.STOP else StoneIcon.RECORD,
        description = stringResource(if (takes.recording) R.string.visuals_stop_take else R.string.visuals_gesture_record),
        enabled = awake || takes.recording,
        onClick = {
            if (takes.recording) {
                studioViewModel.stopRecording()
            } else {
                GestureTips.nudge(GestureTip.HOLD_TO_RECORD)
                studioViewModel.startRecording()
            }
        },
        modifier =
            Modifier
                .align(Alignment.BottomEnd)
                .offset { IntOffset(-endPx().roundToInt(), -sheetTopPx().roundToInt()) }
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.End))
                .padding(GeodeSpacing.EdgeInset)
                .faded(recordShown)
                .then(glass),
        tint = if (takes.recording) MaterialTheme.colorScheme.error else Color.Unspecified,
    )
}

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
        Icon(icon.vector, description, tint = if (tint == Color.Unspecified) LocalContentColor.current else tint)
    }
}

/**
 * What the visual's gestures can do here, for the gesture map to choose from ([VisualActions]).
 * Randomize everything is Customize's roll with no page given (`randomizeParams()`, the native
 * randomizer), which is Pro there; a free user meets the same paywall Customize's roll shows.
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
    val paywall: PaywallViewModel = geodeViewModel()
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
        randomizeAll = {
            if (gate().allows(ProFeature.ADVANCED_CUSTOMIZATION)) {
                visualsViewModel.randomizeParams()
            } else {
                paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION))
            }
        },
        touchPulse = touchPulse,
        playPause = viewModel::togglePlayPause,
    )
}

/** The actions a sideways swipe, or its ‹ › twins, can step through. */
internal val STEP_ACTIONS = setOf(GestureAction.STEP_STYLE, GestureAction.STEP_PRESET, GestureAction.STEP_PLAYLIST)

/**
 * Every style the stage swipes through, in the order the Styles page lists them. MilkDrop is left
 * out: its presets are a library of their own, picked from the MilkDrop page.
 */
internal fun stageStyleOrder(): List<String> =
    (
        StyleFamilies.ids +
            VisualStyleCatalog.silkIds + VisualStyleCatalog.lifeIds + VisualStyleCatalog.mycoIds + VisualStyleCatalog.acidIds +
            SceneCapabilities.SHADER_SCENES.keys.filterNot { it in StyleFamilies.ids } +
            VisualStyleCatalog.fluidIds + listOf(SceneIds.CURLFLOW, SceneIds.WATER) + VisualStyleCatalog.cymaticsIds
    ).distinct()

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
