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
import androidx.compose.runtime.derivedStateOf
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
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.changedToDown
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
import androidx.compose.ui.unit.Dp
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

    /**
     * Set by Visuals while its options sheet is up: the mini player is docked to the sheet and the
     * tab bar under it stays with it, so neither fades on a stage tap. The stage icons still
     * follow [awake].
     */
    var docked by mutableStateOf(false)

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

    /** A touch in the dock: an awake dock starts its sleep timer over, so it never fades mid-reach. */
    fun touched() {
        if (awake) stamp++
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
    // In the same frame as the new screen or track, not an effect a frame later: leaving Visuals
    // asleep would otherwise lay the dock out with the handle first, then grow it into the card.
    remember(*wakeKeys) { state.wake() }
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
 *
 * A swipe must travel [SWIPE_DP] (48 dp, a full touch target): at 24 dp the brush of a tap aimed
 * at play or next was enough to open Player or hide the bar.
 *
 * Unlike the visual ([visualGestures]), it needs no system-strip check. The pill floats above the
 * bottom insets, so no part of it lies in the bottom gesture strip, and a drag that does start in
 * that strip never reaches it: the strip is `WindowInsets.mandatorySystemGestures`, "a subset of
 * the system gestures that will always be handled by the system" (Compose's "About window insets",
 * the inset types table; the edge-to-edge skill keeps touch targets out of the system insets the
 * same way), so Android takes home and recents there before the app sees the touch.
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
    val view = LocalView.current
    val tap = { action: () -> Unit ->
        view.performStoneHaptic(StoneHapticCue.TAP)
        action()
    }
    Box(
        modifier
            .fillMaxWidth()
            .floatingGlass(fillAlpha = opacity)
            .miniPlayerGestures(onPin, onOpenPlayer, onHide, progress, inset = GeodeRadius.Floating),
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
 * The mini player docked to the bottom of the Visuals options (owner: one glass slab, slimmer, no
 * art): one line of title and previous, play/pause and next, each a 48 dp target, over the same
 * progress line, touch and swipes as [FloatingMiniPlayer]. It draws no glass of its own: the
 * options' glass is around it, and with the sheet hidden it is that glass's only content, a pill.
 */
@Composable
internal fun DockedMiniPlayer(
    title: String?,
    isPlaying: Boolean,
    progress: () -> Float,
    onPin: () -> Unit,
    onOpenPlayer: () -> Unit,
    onHide: () -> Unit,
    onPrevious: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val tap = { action: () -> Unit ->
        view.performStoneHaptic(StoneHapticCue.TAP)
        action()
    }
    Row(
        modifier
            .fillMaxWidth()
            .height(DOCKED_PLAYER_HEIGHT)
            .miniPlayerGestures(onPin, onOpenPlayer, onHide, progress, inset = GeodeSpacing.L)
            .padding(start = GeodeSpacing.L, end = GeodeSpacing.XS),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title ?: stringResource(R.string.mini_player_idle),
            Modifier.weight(1f),
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        IconButton(onClick = { tap(onPrevious) }) { StoneIconArt(StoneIcon.PREVIOUS, stringResource(R.string.action_previous)) }
        IconButton(onClick = { tap(onPlayPause) }) {
            StoneIconArt(
                if (isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
                stringResource(if (isPlaying) R.string.action_pause else R.string.action_play),
            )
        }
        IconButton(onClick = { tap(onNext) }) { StoneIconArt(StoneIcon.NEXT, stringResource(R.string.action_next)) }
    }
}

/**
 * Restarts [chrome]'s sleep timer on any touch down inside, without taking the touch: watched on
 * the Initial pass, so the buttons and swipes under it work as before.
 */
internal fun Modifier.restartsSleepTimer(chrome: ShellChromeState): Modifier =
    pointerInput(chrome) {
        awaitPointerEventScope {
            while (true) {
                val event = awaitPointerEvent(PointerEventPass.Initial)
                if (event.changes.any { it.changedToDown() }) chrome.touched()
            }
        }
    }

/** The docked bar's height: a 48 dp target and the progress line, slimmer than the 64 dp card. */
internal val DOCKED_PLAYER_HEIGHT = 52.dp

/**
 * The mini player's touch and its progress line, shared by the floating card and the docked bar:
 * a tap pins, a swipe of [SWIPE_DP] up opens Player and down hides it, TalkBack gets both as
 * actions, and the line runs along the top edge [inset] in from each side.
 */
@Composable
private fun Modifier.miniPlayerGestures(
    onPin: () -> Unit,
    onOpenPlayer: () -> Unit,
    onHide: () -> Unit,
    progress: () -> Float,
    inset: Dp,
): Modifier {
    val accent = MaterialTheme.colorScheme.primary
    val openLabel = stringResource(R.string.mini_player_open)
    val hideLabel = stringResource(R.string.mini_player_hide)
    val pinLabel = stringResource(R.string.mini_player_pin)
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
    return pointerInput(Unit) {
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
    }.clickable(onClickLabel = pinLabel) {
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
        val edge = inset.toPx()
        val width = size.width - 2 * edge
        drawRect(accent.copy(alpha = TRACK_ALPHA), Offset(edge, 0f), Size(width, PROGRESS_DP.dp.toPx()))
        drawRect(accent, Offset(edge, 0f), Size(width * progress().coerceIn(0f, 1f), PROGRESS_DP.dp.toPx()))
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
                    label =
                        if (showLabel) {
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
 *
 * WHY `derivedStateOf`: `LocalWindowInfo.containerSize` is snapshot state that changes on every
 * pixel of a window resize (a freeform window dragged, a foldable mid-unfold), and reading it
 * straight in composition recomposed the tab bar each time. The bar only cares which side of
 * the line the width is on, so the read sits in a `derivedStateOf` and composition hears only
 * the boolean flip. This is the adaptive skill's guidance for window size: `derivedMediaQuery`
 * wraps the query in a `derivedStateOf` "because `windowWidth` and `windowHeight` can update
 * frequently" (adaptive skill, references/.../layouts/adaptive/mediaquery/index.md). We write
 * the `derivedStateOf` by hand because `mediaQuery` is experimental and needs
 * `ComposeUiFlags.isMediaQueryIntegrationEnabled`, which the app does not set. The density is a
 * key, not a read inside, because a font-scale change hands down a new `Density` object.
 */
@Composable
private fun compactTabLabels(): Boolean {
    val windowInfo = LocalWindowInfo.current
    val density = LocalDensity.current
    val compact by remember(windowInfo, density) {
        derivedStateOf {
            with(density) { windowInfo.containerSize.width.toDp() } < COMPACT_TAB_WIDTH ||
                density.fontScale >= COMPACT_TAB_FONT_SCALE
        }
    }
    return compact
}

private const val SWIPE_DP = 48
private const val COMPACT_TAB_FONT_SCALE = 1.5f
private val COMPACT_TAB_WIDTH = 360.dp
private const val PROGRESS_DP = 2
private const val TRACK_ALPHA = 0.2f
private const val HANDLE_ALPHA = 0.7f
private val HANDLE_WIDTH = 36.dp
private val HANDLE_HEIGHT = 4.dp
