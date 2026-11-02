package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MacroStore
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass

/**
 * The Macros drawer: the six macro knobs on a glass panel pulled down from the top of the visual
 * (a swipe down, by default; ui-flow decisions: "Macros live in both places", here and as
 * Customize's first page). The knobs are Customize › Macros' own ([MacrosTab]) over the same
 * [MacroStore], so a turn here is a turn there.
 *
 * The edit icon ([onEdit]) opens Customize on Macros, where the sets and making a macro live; the
 * drawer only turns the knobs. It closes the standard ways: Back, a tap outside, the ×, or pushing
 * it back up. No scrim, as with the quick settings, so the visual stays lit while the knobs move it.
 *
 * It slides on the app's spatial spring ([LocalGeodeMotion]), which is a jump under reduced
 * motion, and fades on the effects spec, which stays. While it is open the options sheet fades out
 * under it (the caller's [hiddenUnderDropIn]), so only the visual shows through its glass.
 */
@Composable
internal fun MacrosDrawer(
    visible: Boolean,
    onDismiss: () -> Unit,
    onEdit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    BackHandler(enabled = visible, onBack = onDismiss)
    val motion = LocalGeodeMotion.current
    Box(modifier) {
        // The outside tap: a bare hit target under the panel, only while it is open, with no
        // semantics of its own; TalkBack users close the drawer with its × or Back.
        if (visible) {
            Box(
                Modifier
                    .fillMaxSize()
                    .pointerInput(onDismiss) { detectTapGestures { onDismiss() } },
            )
        }
        AnimatedVisibility(
            visible = visible,
            modifier = Modifier.align(Alignment.TopCenter),
            enter = slideInVertically(motion.defaultSpatialSpec()) { -it } + fadeIn(motion.fastEffectsSpec()),
            exit = slideOutVertically(motion.fastSpatialSpec()) { -it } + fadeOut(motion.fastEffectsSpec()),
        ) {
            MacrosDrawerPanel(onDismiss, onEdit)
        }
    }
}

@Composable
private fun MacrosDrawerPanel(
    onDismiss: () -> Unit,
    onEdit: () -> Unit,
) {
    val context = LocalContext.current
    val store = remember { MacroStore.get(context) }
    val macros by store.state.collectAsStateWithLifecycle()
    val closePx = with(LocalDensity.current) { CLOSE_DRAG.toPx() }
    var dragged by remember { mutableFloatStateOf(0f) }
    val drag = rememberDraggableState { delta -> dragged += delta }
    val title = stringResource(R.string.macros_drawer_title)
    val close = stringResource(R.string.action_close)
    Column(
        Modifier
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))
            .padding(GeodeSpacing.EdgeInset)
            .widthIn(max = PANEL_MAX_WIDTH)
            .fillMaxWidth()
            .floatingGlass(fillAlpha = DROP_IN_FILL)
            // Taps on the panel's empty parts stay on it instead of reaching the outside target.
            .pointerInput(Unit) { detectTapGestures { } }
            // Drags that start here stay here: the stage and the mini player never see them.
            .keepDragsInside()
            // Pushed back up, it closes; the knobs take their own vertical drags first.
            .draggable(
                drag,
                Orientation.Vertical,
                onDragStarted = { dragged = 0f },
                onDragStopped = { velocity -> if (dragged < -closePx || velocity < -FLING_PX_PER_S) onDismiss() },
            ).semantics {
                paneTitle = title
                customActions = listOf(CustomAccessibilityAction(close) { true.also { onDismiss() } })
            }.padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
            IconButton(onClick = onEdit) {
                StoneIconArt(StoneIcon.EDIT, stringResource(R.string.macros_drawer_edit))
            }
            IconButton(onClick = onDismiss) {
                StoneIconArt(StoneIcon.CLOSE, close)
            }
        }
        MacrosTab(macros, store)
    }
}

private val PANEL_MAX_WIDTH = 420.dp
private val CLOSE_DRAG = 48.dp
private const val FLING_PX_PER_S = 1200f
