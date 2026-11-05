@file:Suppress("MatchingDeclarationName")

package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalAccessibilityManager
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.tour.GestureTip
import dev.geode.ui.tour.GestureTips
import kotlinx.coroutines.delay

/**
 * Whether the floating mini player (and, on Visuals, the stage icons and the tab bar) is awake.
 *
 * WHY it fades on every screen (docs/ui-redesign): the owner wants the screen to belong to what is
 * on it, and the player back within one touch. It wakes on [wake] (a tap at the bottom of the
 * visual, the handle pill, a new track, a new screen) and falls asleep [fadeMs] later; [pin]
 * holds it awake for [PIN_MS] (a tap on the player itself), [hide] puts it to sleep at once (a
 * swipe down on it). A [fadeMs] of zero means it never fades. While [held] (a PiP window, which
 * shows the visual alone) nothing wakes it.
 */
@Stable
internal class ShellChromeState {
    private var shown by mutableStateOf(true)

    /** Set by [rememberShellChrome] while a PiP window is up; a wake then waits until it ends. */
    internal var held by mutableStateOf(false)

    val awake: Boolean get() = shown && !held

    /** Bumped on every wake or pin, so the sleep timer starts over. */
    internal var stamp by mutableLongStateOf(0L)
        private set

    internal var pinned by mutableStateOf(false)
        private set

    fun wake() {
        shown = true
        pinned = false
        stamp++
    }

    fun pin() {
        shown = true
        pinned = true
        stamp++
    }

    fun hide() {
        shown = false
        pinned = false
    }

    internal fun sleep() {
        shown = false
        pinned = false
    }

    companion object {
        /** How long a pinned player stays: "for a while", long enough to read and reach it. */
        const val PIN_MS = 30_000L
    }
}

/**
 * The shell's chrome state, asleep [fadeSeconds] after each wake (the user's Settings value,
 * 0 = never), stretched by the accessibility timeout the user set so TalkBack users are not raced.
 * Any change in [wakeKeys] (the screen, the track) wakes it. While [held] it stays asleep.
 */
@Composable
internal fun rememberShellChrome(
    fadeSeconds: Float,
    held: Boolean,
    vararg wakeKeys: Any?,
): ShellChromeState {
    val state = remember { ShellChromeState() }
    SideEffect { state.held = held }
    val a11y = LocalAccessibilityManager.current
    LaunchedEffect(*wakeKeys) { state.wake() }
    LaunchedEffect(state.awake, state.stamp, fadeSeconds) {
        if (!state.awake || fadeSeconds <= 0f) return@LaunchedEffect
        val base = if (state.pinned) ShellChromeState.PIN_MS else (fadeSeconds * MILLIS_PER_SECOND).toLong()
        val timeout = a11y?.calculateRecommendedTimeoutMillis(base, containsIcons = true, containsControls = true) ?: base
        delay(timeout)
        state.sleep()
    }
    return state
}

private const val MILLIS_PER_SECOND = 1000f

/**
 * The floating mini player: art, title · artist, play/pause and next over a thin progress line,
 * on matte glass. A tap pins it, a swipe up opens Player, a swipe down hides it (the music plays
 * on). The buttons are the only way to skip, so a sideways brush never changes the song.
 */
@Composable
internal fun FloatingMiniPlayer(
    title: String?,
    artist: String?,
    artworkUri: String?,
    isPlaying: Boolean,
    progress: () -> Float,
    opacity: Float,
    onPin: () -> Unit,
    onOpenPlayer: () -> Unit,
    onHide: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val accent = MaterialTheme.colorScheme.primary
    val openLabel = stringResource(R.string.mini_player_open)
    val hideLabel = stringResource(R.string.mini_player_hide)
    val view = LocalView.current
    // A tap is a light tick; a swipe that changes the screen (Player opens, the player hides) confirms.
    val tap = { action: () -> Unit ->
        view.performStoneHaptic(StoneHapticCue.TAP)
        action()
    }
    val confirm = { action: () -> Unit ->
        view.performStoneHaptic(StoneHapticCue.CONFIRM)
        action()
    }
    Box(
        modifier
            .fillMaxWidth()
            .floatingGlass(fillAlpha = opacity)
            .pointerInput(Unit) {
                var dy = 0f
                detectVerticalDragGestures(
                    onDragStart = { dy = 0f },
                    onDragEnd = {
                        val far = SWIPE_DP.dp.toPx()
                        when {
                            dy < -far -> confirm(onOpenPlayer)
                            dy > far -> confirm(onHide)
                        }
                    },
                ) { _, delta -> dy += delta }
            }.clickable(onClickLabel = stringResource(R.string.mini_player_pin)) {
                // A tap is the long way to Player: the tip says a swipe up opens it.
                GestureTips.nudge(GestureTip.SWIPE_UP_PLAYER)
                tap(onPin)
            }.semantics {
                customActions =
                    listOf(
                        CustomAccessibilityAction(openLabel) {
                            onOpenPlayer()
                            true
                        },
                        CustomAccessibilityAction(hideLabel) {
                            onHide()
                            true
                        },
                    )
            }.drawBehind {
                // The progress line along the top edge, inside the glass's corners.
                val inset = GeodeRadius.Floating.toPx()
                val width = size.width - 2 * inset
                drawRect(accent.copy(alpha = TRACK_ALPHA), Offset(inset, 0f), Size(width, PROGRESS_DP.dp.toPx()))
                drawRect(accent, Offset(inset, 0f), Size(width * progress().coerceIn(0f, 1f), PROGRESS_DP.dp.toPx()))
            },
    ) {
        Row(
            Modifier.fillMaxWidth().heightIn(min = 64.dp).padding(start = GeodeSpacing.S, end = GeodeSpacing.XS),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TrackArtwork(artworkUri, Modifier.size(48.dp), corner = GeodeRadius.Medium)
            Column(Modifier.weight(1f).padding(horizontal = GeodeSpacing.M)) {
                Text(
                    title ?: stringResource(R.string.mini_player_idle),
                    style = MaterialTheme.typography.titleSmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (!artist.isNullOrBlank()) {
                    Text(
                        artist,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            IconButton(onClick = { tap(onPlayPause) }) {
                StoneIconArt(
                    if (isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
                    stringResource(if (isPlaying) R.string.action_pause else R.string.action_play),
                )
            }
            IconButton(onClick = { tap(onNext) }) { StoneIconArt(StoneIcon.NEXT, stringResource(R.string.action_next)) }
        }
    }
}

/**
 * What is left of the mini player while it sleeps: a small pill in a full 48 dp target. A tap or
 * a swipe up wakes it.
 */
@Composable
internal fun MiniPlayerHandle(
    onWake: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val label = stringResource(R.string.mini_player_show)
    val view = LocalView.current
    Box(
        modifier
            .fillMaxWidth()
            .height(GeodeSpacing.TouchTarget)
            .pointerInput(Unit) {
                detectVerticalDragGestures { _, delta -> if (delta < 0f) onWake() }
            }.clickable(onClickLabel = label) {
                view.performStoneHaptic(StoneHapticCue.TAP)
                onWake()
            }.semantics {
                onClick(label) {
                    onWake()
                    true
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .width(HANDLE_WIDTH)
                .height(HANDLE_HEIGHT)
                .background(MaterialTheme.colorScheme.primary.copy(alpha = HANDLE_ALPHA), RoundedCornerShape(50)),
        )
    }
}

/** One tab of the dock's bar and the wide shell's rail: its label and its icon. */
internal data class DockItem(
    val label: String,
    val icon: StoneIcon,
)

/**
 * The tab bar as a floating pill of matte glass: the standard M3 NavigationBar (Android's own
 * bottom navigation), with no window insets of its own because the pill sits above them.
 *
 * WHY the labels can step aside (#558): five `labelSmall` labels in a pill share less than a
 * fifth of the window each, so at a large font or on a narrow phone they overran and clipped.
 * Compose's "Support user-scalable content" guide asks that content reflow to fit its container
 * rather than be cut off (Context7 `/websites/developer_android_develop_ui_compose`,
 * accessibility/scalable-content), so when [compactTabLabels] says the room is short only the
 * selected tab keeps its label, the M3 pattern `NavigationBarItem`'s `alwaysShowLabel = false`
 * stands for (components/navigation-bar: the label is optional, the icon carries the item). We
 * drop the unselected labels outright (`label = null`) rather than set `alwaysShowLabel = false`,
 * because that flag only fades the label to alpha 0 and still lays it out, which is the
 * overflow we are removing; the name moves to the icon's content description instead, so
 * TalkBack reads every tab exactly once whichever way it is drawn. Every label that is drawn
 * still ends in an ellipsis (`TextOverflow.Ellipsis`, text/configure-layout: "indicate a
 * TextOverflow, which is only shown if the displayed text is truncated"), the last guard for a
 * translation longer than its slot.
 *
 * The height stays safe at large fonts: in the compact look at most one label is drawn, one
 * line high, and the shell measures the pill (`onSizeChanged` in `AppShell`) rather than
 * assuming its height, so whatever height the bar takes the screen above it makes room.
 */
@Composable
internal fun FloatingTabBar(
    items: List<DockItem>,
    selected: Int,
    onSelect: (Int) -> Unit,
    opacity: Float,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val compact = compactTabLabels()
    Box(modifier.fillMaxWidth().floatingGlass(RoundedCornerShape(50), fillAlpha = opacity)) {
        NavigationBar(containerColor = Color.Transparent, tonalElevation = 0.dp, windowInsets = WindowInsets(0, 0, 0, 0)) {
            items.forEachIndexed { i, item ->
                val isSelected = i == selected
                val showLabel = !compact || isSelected
                NavigationBarItem(
                    selected = isSelected,
                    onClick = {
                        view.performStoneHaptic(StoneHapticCue.TAP)
                        onSelect(i)
                    },
                    // A drawn label names the item for TalkBack (the item merges its children);
                    // an icon-only item names itself, so the name is read once either way.
                    icon = { StoneIconArt(item.icon, if (showLabel) null else item.label, selected = isSelected) },
                    label = if (showLabel) {
                        {
                            Text(
                                item.label,
                                style = MaterialTheme.typography.labelSmall,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                    } else {
                        null
                    },
                )
            }
        }
    }
}

/**
 * Whether the tab bar is short of room for five labels: a window under [COMPACT_TAB_WIDTH] wide,
 * or a font scale of [COMPACT_TAB_FONT_SCALE] or more. WHY the window and not the pill: the pill
 * is inset from the window's edges, so measuring it would put every common 360 dp phone into the
 * compact look at font scale 1.0, which keeps today's look there. WHY 1.5: Android's font
 * scaling is non-linear (accessibility/scalable-content) but small text such as `labelSmall`
 * grows almost in step with the scale, and from 1.5 the longest of the five labels no longer
 * fits its fifth of a 360 dp pill.
 */
@Composable
private fun compactTabLabels(): Boolean {
    val density = LocalDensity.current
    val windowWidth = with(density) { LocalWindowInfo.current.containerSize.width.toDp() }
    return windowWidth < COMPACT_TAB_WIDTH || density.fontScale >= COMPACT_TAB_FONT_SCALE
}

private const val SWIPE_DP = 24
private const val COMPACT_TAB_FONT_SCALE = 1.5f
private val COMPACT_TAB_WIDTH = 360.dp
private const val PROGRESS_DP = 2
private const val TRACK_ALPHA = 0.2f
private const val HANDLE_ALPHA = 0.7f
private val HANDLE_WIDTH = 36.dp
private val HANDLE_HEIGHT = 4.dp
