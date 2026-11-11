package dev.geode.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bedtime
import androidx.compose.material.icons.filled.Cast
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.IconToggleButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.collapse
import androidx.compose.ui.semantics.expand
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.media3.common.Player
import androidx.window.core.layout.WindowSizeClass
import dev.geode.R
import dev.geode.model.QueueTrack
import dev.geode.model.QueueUiState
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.selectedGlow

@Composable
fun PlayerScreen(
    viewModel: PlayerViewModel,
    onOpenSearch: () -> Unit,
    onExpand: () -> Unit,
    onOpenLibrary: () -> Unit,
    showTimeLeft: Boolean = false,
) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    // Not delegated: only the seek bar and the clock read .value, so a tick recomposes just those.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val mic by viewModel.micState.collectAsStateWithLifecycle()
    val external by viewModel.externalAudio.collectAsStateWithLifecycle()
    val abLoop by viewModel.abLoop.collectAsStateWithLifecycle()
    val queue by viewModel.queue.collectAsStateWithLifecycle()
    val favourites by viewModel.favourites.collectAsStateWithLifecycle()
    val tick by viewModel.historyTick.collectAsStateWithLifecycle()
    val sleepRemainingMs by viewModel.sleepTimerRemainingMs.collectAsStateWithLifecycle()
    val canShuffle = remember(tick) { viewModel.recentlyPlayed().isNotEmpty() }
    // The secondary groups under the transport are an accordion: all closed at first, so the
    // screen opens as an overview, and at most one open, so it never grows into a long column.
    // The art, title, seek bar and transport are never inside it.
    var openCard by rememberSaveable { mutableStateOf<String?>(null) }
    val toggleCard: (String) -> Unit = { key -> openCard = if (openCard == key) null else key }
    // The whole queue (reorder, remove, save as a playlist), which used to be a panel of the live view.
    var queueSheet by rememberSaveable { mutableStateOf(false) }
    val upNext = remember(queue) { queue.tracks.drop(queue.index + 1).take(3) }
    val upNextCount = (queue.tracks.size - queue.index - 1).coerceAtLeast(0)
    val sizeClass = currentWindowAdaptiveInfo().windowSizeClass
    val expandedWidth = sizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_EXPANDED_LOWER_BOUND)
    // A phone on its side: wide enough for two columns, too short for one to reach the transport.
    val landscape =
        sizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND) &&
            !sizeClass.isHeightAtLeastBreakpoint(WindowSizeClass.HEIGHT_DP_MEDIUM_LOWER_BOUND)
    // Every block stops at a readable width, so a tablet column is not one stretched strip.
    val readable = Modifier.widthIn(max = COLUMN_MAX_WIDTH)
    val inset = readable.padding(horizontal = SCREEN_INSET)

    val header: @Composable () -> Unit = {
        Row(
            // The title starts on the cards' edge and the search glyph ends on it.
            readable.fillMaxWidth().padding(start = SCREEN_INSET, end = ICON_ROW_INSET).padding(vertical = GeodeSpacing.S),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    stringResource(R.string.app_name),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    stringResource(R.string.nav_player),
                    style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.semantics { heading() },
                )
            }
            IconButton(onClick = onOpenSearch) { StoneIconArt(StoneIcon.SEARCH, stringResource(R.string.action_search)) }
        }
    }
    val hero: @Composable () -> Unit = {
        PlayerHero(
            viewModel = viewModel,
            state = state,
            styleLabel = sceneDisplayLabel(viz.sceneId),
            micActive = mic.active,
            external = external,
            favourites = favourites,
            canResume = canShuffle,
            onExpand = onExpand,
            onOpenLibrary = onOpenLibrary,
            modifier = inset,
        )
    }
    val transport: @Composable () -> Unit = {
        TransportCard(
            viewModel = viewModel,
            state = state,
            position = { position.value },
            abLoop = abLoop,
            showTimeLeft = showTimeLeft,
            modifier = inset,
        )
    }

    // The secondary cards, which take the other side in two columns.
    val cards: LazyListScope.() -> Unit = {
        if (upNext.isNotEmpty()) {
            item {
                UpNextCard(
                    upNext = upNext,
                    count = upNextCount,
                    expanded = openCard == CARD_UP_NEXT,
                    onToggle = { toggleCard(CARD_UP_NEXT) },
                    onOpenQueue = { queueSheet = true },
                    modifier = inset,
                )
            }
        }

        item {
            QuickActionsCard(
                viewModel = viewModel,
                micActive = mic.active,
                external = external,
                sleepRunning = sleepRemainingMs != null,
                canShuffle = canShuffle,
                expanded = openCard == CARD_QUICK,
                onToggle = { toggleCard(CARD_QUICK) },
                modifier = inset,
            )
        }
    }

    // The pane ends above the floating dock (the phone shell's reserve), as every other screen
    // does: the dock's glass is translucent, so a card left under it read through the tab labels.
    BoxWithConstraints(Modifier.fillMaxSize().padding(bottom = LocalDockReserve.current)) {
        // Two columns in landscape or at expanded width: art and transport on one side, the cards
        // on the other. The window says when; this pane's width says whether there is room, so the
        // Player half of the two-pane Library stays one column.
        val sideBySide =
            (expandedWidth && maxWidth >= SIDE_BY_SIDE_MIN_WIDTH) || (landscape && maxWidth >= LANDSCAPE_MIN_WIDTH)
        // Read here: inside the Row below, BoxWithConstraints' scope is not an implicit receiver.
        val paneHeight = maxHeight
        // Under the art when the pane is tall enough for both; on a phone on its side it heads the
        // other column instead, where it is never below the fold.
        val transportWithArt = paneHeight >= SHORT_HEIGHT
        if (sideBySide) {
            Row(Modifier.fillMaxSize()) {
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .verticalScroll(rememberScrollState())
                        .padding(bottom = CARD_GAP),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    AboveTheFold(paneHeight, header, hero, if (transportWithArt) transport else null)
                }
                LazyColumn(
                    Modifier.weight(1f).fillMaxHeight(),
                    contentPadding = PaddingValues(top = GeodeSpacing.S, bottom = CARD_GAP),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(CARD_GAP),
                ) {
                    if (!transportWithArt) item { transport() }
                    cards()
                }
            }
        } else {
            LazyColumn(
                Modifier.fillMaxSize(),
                contentPadding = PaddingValues(bottom = CARD_GAP),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(CARD_GAP),
            ) {
                item { AboveTheFold(paneHeight, header, hero, transport) }
                cards()
            }
        }
    }
    if (queueSheet) {
        QueueSheet(viewModel, queue, favourites, onDismiss = { queueSheet = false })
    }
}

/**
 * The header, the hero and the transport as the pane's first screenful, in one measure pass: the
 * header and the transport are measured first and the hero gets what is left of [paneHeight] as
 * its height limit, which its art (the one weighted child) shrinks into. So the art is as wide as
 * the card when the pane is tall enough, the transport still ends above the fold when it is not,
 * and at a large font the art gives way while the text and the transport grow (#558).
 *
 * WHY constraints and not an art size worked out from constants: the old 300 dp guess at "the
 * chrome around the art" was 80 dp more than the header and title rows take on a phone, and the
 * art was drawn that much smaller than the room it had.
 */
@Composable
private fun AboveTheFold(
    paneHeight: Dp,
    header: @Composable () -> Unit,
    hero: @Composable () -> Unit,
    transport: (@Composable () -> Unit)?,
) {
    Layout(
        content = {
            header()
            hero()
            transport?.invoke()
        },
    ) { measurables, constraints ->
        // The pane scrolls, so the height is unbounded; only the width is the column's.
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val gap = CARD_GAP.roundToPx()
        val headerPlaceable = measurables[HEADER_SLOT].measure(loose)
        val transportPlaceable = measurables.getOrNull(TRANSPORT_SLOT)?.measure(loose)
        val below = transportPlaceable?.let { it.height + gap } ?: 0
        val heroRoom = maxOf(paneHeight.roundToPx() - gap - headerPlaceable.height - below, MIN_HERO.roundToPx())
        val heroPlaceable = measurables[HERO_SLOT].measure(loose.copy(maxHeight = heroRoom))
        val width =
            maxOf(headerPlaceable.width, heroPlaceable.width, transportPlaceable?.width ?: 0)
                .coerceIn(constraints.minWidth, constraints.maxWidth)
        layout(width, headerPlaceable.height + heroPlaceable.height + below) {
            headerPlaceable.placeRelative((width - headerPlaceable.width) / 2, 0)
            heroPlaceable.placeRelative((width - heroPlaceable.width) / 2, headerPlaceable.height)
            transportPlaceable?.placeRelative(
                (width - transportPlaceable.width) / 2,
                headerPlaceable.height + heroPlaceable.height + gap,
            )
        }
    }
}

/** The children of [AboveTheFold], in the order it emits them. */
private const val HEADER_SLOT = 0
private const val HERO_SLOT = 1
private const val TRANSPORT_SLOT = 2

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun QueueSheet(
    viewModel: PlayerViewModel,
    queue: QueueUiState,
    favourites: Set<String>,
    onDismiss: () -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        QueuePanel(
            queue = queue,
            favourites = favourites,
            onPlayIndex = viewModel::playQueueIndex,
            onMoveUp = { viewModel.moveQueueItem(it, it - 1) },
            onRemove = viewModel::removeQueueItem,
            modifier = Modifier.fillMaxWidth().fillMaxHeight(QUEUE_SHEET_HEIGHT),
        )
    }
}

private const val QUEUE_SHEET_HEIGHT = 0.85f

/** The accordion's card keys, saved by name. */
private const val CARD_UP_NEXT = "up_next"
private const val CARD_QUICK = "quick"

/** At expanded width, a pane narrower than this (the Player half of the two-pane Library) keeps one column. */
private val SIDE_BY_SIDE_MIN_WIDTH = 720.dp

/** In landscape, the least pane width that still gives two usable columns. */
private val LANDSCAPE_MIN_WIDTH = 520.dp

/** A pane shorter than this is a phone on its side: the transport moves out from under the art. */
private val SHORT_HEIGHT = 480.dp

/** A readable column: wider than this, a block stops growing and centres. */
private val COLUMN_MAX_WIDTH = 600.dp

/** On a phone the art fills the card's width; on anything wider it stops here. */
private val MAX_ART = 420.dp

/**
 * The least height the hero is given, however short the pane: below it the pane scrolls instead
 * of squeezing the art further (about 160 dp of art at the default font).
 */
private val MIN_HERO = 280.dp

/** The cards' inset from the pane's edges and the content's inset inside a card: one grid. */
private val SCREEN_INSET = GeodeSpacing.L
private val CARD_INSET = GeodeSpacing.L

/**
 * The inset of a row that ends (or starts) with icon buttons: a 24 dp glyph sits 12 dp inside its
 * 48 dp target, so the glyph's edge lands on [CARD_INSET], where the text beside it ends.
 */
private val ICON_ROW_INSET = GeodeSpacing.XS

/** Between cards, and between the blocks inside one. */
private val CARD_GAP = GeodeSpacing.M

private val CARD_SHAPE = RoundedCornerShape(GeodeRadius.ExtraLarge)
private val TILE_SHAPE = RoundedCornerShape(GeodeRadius.Large)

@Composable
private fun PlayerHero(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    styleLabel: String,
    micActive: Boolean,
    external: ExternalAudioState,
    favourites: Set<String>,
    canResume: Boolean,
    onExpand: () -> Unit,
    onOpenLibrary: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val uri = state.trackUri
    val foreign = external.active
    val foreignTrack = external.nowPlaying?.takeIf { it.title.isNotBlank() }
    // The mic is a layer over playback: with a track loaded the hero stays the track's.
    val roomOnly = micActive && !state.hasMedia
    val hasSource = foreign || roomOnly || state.hasMedia
    val isFavourite = uri != null && uri in favourites
    val showFavourite = state.hasMedia && !foreign
    Column(
        modifier
            .fillMaxWidth()
            .clip(CARD_SHAPE)
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .clickable(enabled = hasSource, onClick = onExpand)
            .padding(vertical = CARD_INSET),
        verticalArrangement = Arrangement.spacedBy(CARD_GAP),
    ) {
        // Square and as wide as the card, unless the height [AboveTheFold] leaves is less: the one
        // weighted child, so it takes what the rows under it do not.
        TrackArtwork(
            if (foreign || roomOnly) null else uri,
            Modifier
                .align(Alignment.CenterHorizontally)
                .weight(1f, fill = false)
                .padding(horizontal = CARD_INSET)
                .widthIn(max = MAX_ART)
                .aspectRatio(1f),
            corner = GeodeRadius.Medium,
        )
        Column(verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS)) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = CARD_INSET),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    when {
                        foreign -> external.nowPlaying?.appLabel ?: stringResource(R.string.source_other_apps)
                        roomOnly -> stringResource(R.string.source_live_input)
                        state.isPlaying -> stringResource(R.string.state_now_playing)
                        state.hasMedia -> stringResource(R.string.state_paused)
                        else -> stringResource(R.string.state_nothing_playing)
                    },
                    Modifier.weight(1f),
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                SceneChip(styleLabel, Modifier.weight(1f, fill = false).padding(start = GeodeSpacing.S))
            }
            Row(
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .padding(start = CARD_INSET, end = if (showFavourite) ICON_ROW_INSET else CARD_INSET),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        when {
                            foreign -> foreignTrack?.title ?: stringResource(R.string.title_whatever_is_playing)
                            roomOnly -> stringResource(R.string.title_the_room)
                            state.hasMedia -> state.title ?: stringResource(R.string.title_untitled)
                            else -> stringResource(R.string.title_pick_something)
                        },
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        when {
                            external.refusedByApp ->
                                stringResource(
                                    R.string.subtitle_capture_refused,
                                    external.refusingApp
                                        ?: stringResource(R.string.subtitle_capture_refused_unknown_app),
                                )
                            foreign && external.hearingSilence -> stringResource(R.string.subtitle_capture_silent)
                            foreign ->
                                foreignTrack?.artist?.ifBlank { null }
                                    ?: stringResource(R.string.subtitle_captured_from_another_app)
                            roomOnly -> stringResource(R.string.subtitle_microphone_hears)
                            state.hasMedia ->
                                state.artist?.takeIf { it.isNotBlank() }
                                    ?: stringResource(R.string.subtitle_unknown_artist)
                            else -> stringResource(R.string.subtitle_nothing_playing)
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color =
                            if (external.refusedByApp) {
                                MaterialTheme.colorScheme.error
                            } else {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            },
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                if (showFavourite) {
                    IconButton(onClick = { viewModel.toggleFavourite(uri) }) {
                        StoneIconArt(
                            StoneIcon.FAVORITE,
                            stringResource(
                                if (isFavourite) R.string.action_favourite_remove else R.string.action_favourite_add,
                            ),
                            selected = isFavourite,
                            tint =
                                if (isFavourite) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                        )
                    }
                }
            }
        }
        if (foreign) {
            Row(Modifier.fillMaxWidth().padding(horizontal = CARD_INSET), horizontalArrangement = Arrangement.End) {
                FilledTonalButton(
                    onClick = viewModel::stopExternalAudio,
                    contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
                ) {
                    Text(stringResource(R.string.action_stop_capture))
                }
            }
        }
        if (!hasSource) {
            Row(
                Modifier.padding(horizontal = CARD_INSET),
                horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
            ) {
                Button(onClick = viewModel::resumeLastPlayed, enabled = canResume) {
                    Text(stringResource(R.string.action_resume_last_played))
                }
                FilledTonalButton(onClick = onOpenLibrary) { Text(stringResource(R.string.action_open_library)) }
            }
        }
    }
}

/** The style on screen, on a raised fill so its text's inset reads as a chip's, not as a stray indent. */
@Composable
private fun SceneChip(
    label: String,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier
            .clip(RoundedCornerShape(GeodeRadius.Medium))
            .background(MaterialTheme.colorScheme.surfaceContainerHighest)
            .padding(horizontal = GeodeSpacing.S, vertical = GeodeSpacing.XS),
    ) {
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = accentTextColor(),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun TransportCard(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    position: () -> Long,
    abLoop: AbLoop?,
    showTimeLeft: Boolean,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .fillMaxWidth()
            .clip(CARD_SHAPE)
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .padding(top = GeodeSpacing.S, bottom = GeodeSpacing.M),
    ) {
        // The bar keeps its 48 dp target; the times and A–B sit under the track, over the lower
        // edge of that target, so the three read as one block and A–B needs no row of its own.
        Box(Modifier.fillMaxWidth().padding(horizontal = CARD_INSET)) {
            TrackSeekBar(
                positionMs = position,
                durationMs = state.durationMs,
                loopStartMs = abLoop?.startMs,
                loopEndMs = abLoop?.endMs,
                onSeek = viewModel::seekTo,
                modifier = Modifier.fillMaxWidth().height(GeodeSpacing.TouchTarget),
            )
            Row(
                Modifier.fillMaxWidth().padding(top = GeodeSpacing.TouchTarget - GeodeSpacing.M),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                PositionClock(position)
                Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
                    TextButton(onClick = viewModel::cycleAbLoop, enabled = state.hasMedia) {
                        Text(
                            when {
                                abLoop == null -> stringResource(R.string.ab_loop_idle)
                                abLoop.endMs == null -> stringResource(R.string.ab_loop_set_b)
                                else -> stringResource(R.string.ab_loop_looping)
                            },
                            style = MaterialTheme.typography.labelMedium,
                            color =
                                if (abLoop != null) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                        )
                    }
                }
                if (showTimeLeft) {
                    PositionClock(position) { formatTimeLeft(it, state.durationMs) }
                } else {
                    Text(formatClock(state.durationMs), style = MaterialTheme.typography.labelSmall)
                }
            }
        }
        // Edge to edge on the card's grid: the shuffle and repeat glyphs end where the times do.
        Row(
            Modifier.fillMaxWidth().padding(horizontal = ICON_ROW_INSET),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            ShuffleButton(state.shuffle, viewModel::toggleShuffle)
            IconButton(onClick = viewModel::previous, enabled = state.hasMedia) {
                StoneIconArt(StoneIcon.PREVIOUS, stringResource(R.string.action_previous))
            }
            // 64 dp, the size the old play button drew itself at, so the transport row keeps its rhythm.
            FilledIconButton(
                onClick = viewModel::togglePlayPause,
                modifier = Modifier.size(64.dp),
                enabled = state.hasMedia,
            ) {
                StoneIconArt(
                    if (state.isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
                    stringResource(if (state.isPlaying) R.string.action_pause else R.string.action_play),
                )
            }
            IconButton(onClick = viewModel::next, enabled = state.hasMedia) {
                StoneIconArt(StoneIcon.NEXT, stringResource(R.string.action_next))
            }
            RepeatButton(state.repeatMode, viewModel::cycleRepeatMode)
        }
    }
}

/**
 * One card of the Player's accordion: a 56 dp header row (name, the current value on the right,
 * a chevron that turns when open) that expands its content in place. TalkBack hears a button with
 * an expand or collapse action, so it announces the state in the system's own words.
 */
@Composable
private fun CollapsibleCard(
    title: String,
    summary: String,
    expanded: Boolean,
    onToggle: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val chevronTurn by animateFloatAsState(if (expanded) 180f else 0f, motion.fastSpatialSpec(), label = "chevron")
    Column(
        modifier
            .fillMaxWidth()
            .clip(CARD_SHAPE)
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .animateContentSize(motion.defaultSpatialSpec()),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = 56.dp)
                .semantics {
                    if (expanded) {
                        collapse {
                            onToggle()
                            true
                        }
                    } else {
                        expand {
                            onToggle()
                            true
                        }
                    }
                }.clickable(role = Role.Button, onClick = onToggle)
                .padding(horizontal = CARD_INSET),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(
                summary,
                Modifier.weight(1f).padding(horizontal = GeodeSpacing.M),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.End,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(
                Icons.Filled.KeyboardArrowDown,
                contentDescription = null,
                modifier = Modifier.size(24.dp).rotate(chevronTurn),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (expanded) content()
    }
}

@Composable
private fun UpNextCard(
    upNext: List<QueueTrack>,
    count: Int,
    expanded: Boolean,
    onToggle: () -> Unit,
    onOpenQueue: () -> Unit,
    modifier: Modifier = Modifier,
) {
    CollapsibleCard(
        title = stringResource(R.string.queue_up_next),
        summary = pluralStringResource(R.plurals.track_count, count, count),
        expanded = expanded,
        onToggle = onToggle,
        modifier = modifier,
    ) {
        Column(
            Modifier.fillMaxWidth().padding(start = CARD_INSET, end = CARD_INSET, bottom = GeodeSpacing.XS),
        ) {
            val untitled = stringResource(R.string.title_untitled)
            upNext.forEach { t ->
                Text(
                    t.title.ifBlank { untitled },
                    Modifier
                        .fillMaxWidth()
                        .clickable(onClick = onOpenQueue)
                        .heightIn(min = GeodeSpacing.TouchTarget)
                        .wrapContentHeight(),
                    style = MaterialTheme.typography.bodyMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            // The button's own 12 dp padding puts its label on the card's inset.
            Row(Modifier.fillMaxWidth().offset(x = GeodeSpacing.M), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = onOpenQueue) {
                    Text(
                        stringResource(R.string.action_open_queue),
                        style = MaterialTheme.typography.labelMedium,
                        color = accentTextColor(),
                    )
                }
            }
        }
    }
}

/**
 * The quick actions (live input, other apps' audio, the sleep timer, shuffle all) as a card.
 * Closed, its summary names what is on, or the actions it holds when nothing is.
 */
@Composable
private fun QuickActionsCard(
    viewModel: PlayerViewModel,
    micActive: Boolean,
    external: ExternalAudioState,
    sleepRunning: Boolean,
    canShuffle: Boolean,
    expanded: Boolean,
    onToggle: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val active =
        listOfNotNull(
            stringResource(R.string.quick_room_on).takeIf { micActive },
            stringResource(R.string.quick_capturing).takeIf { external.active },
            stringResource(R.string.quick_sleep_on).takeIf { sleepRunning },
        )
    val offered =
        listOfNotNull(
            stringResource(R.string.source_live_input),
            stringResource(R.string.source_other_apps),
            stringResource(R.string.quick_sleep_30m),
            stringResource(R.string.quick_shuffle_all),
        )
    CollapsibleCard(
        title = stringResource(R.string.action_more),
        summary = active.ifEmpty { offered }.joinToString(" · "),
        expanded = expanded,
        onToggle = onToggle,
        modifier = modifier,
    ) {
        QuickActions(
            viewModel = viewModel,
            micActive = micActive,
            external = external,
            sleepRunning = sleepRunning,
            canShuffle = canShuffle,
            contentPadding = PaddingValues(start = CARD_INSET, end = CARD_INSET, bottom = CARD_INSET),
        )
    }
}

@Composable
private fun QuickActions(
    viewModel: PlayerViewModel,
    micActive: Boolean,
    external: ExternalAudioState,
    sleepRunning: Boolean,
    canShuffle: Boolean,
    contentPadding: PaddingValues,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    // Once Android stops showing the dialog, request() opens the app's Settings page instead, so
    // the button never silently does nothing.
    val micPermission =
        rememberPermissionRequest(android.Manifest.permission.RECORD_AUDIO) { viewModel.setMicEnabled(true) }
    val projectionLauncher =
        androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts
                .StartActivityForResult(),
        ) { result ->
            val data = result.data
            if (result.resultCode == android.app.Activity.RESULT_OK && data != null) {
                dev.geode.audio.PlaybackCaptureService
                    .start(context, result.resultCode, data)
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }
    val capturePermissions =
        androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts
                .RequestMultiplePermissions(),
        ) { granted ->
            if (granted[android.Manifest.permission.RECORD_AUDIO] != false) {
                viewModel.noteExternalAudioConsentPending()
                projectionLauncher.launch(
                    context
                        .getSystemService(android.media.projection.MediaProjectionManager::class.java)
                        .createScreenCaptureIntent(),
                )
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }
    LazyRow(
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        contentPadding = contentPadding,
    ) {
        item {
            QuickAction(
                StoneIcon.MICROPHONE,
                stringResource(if (micActive) R.string.quick_room_on else R.string.source_live_input),
                active = micActive,
            ) {
                if (micActive) {
                    viewModel.setMicEnabled(false)
                } else if (viewModel.hasMicPermission()) {
                    viewModel.setMicEnabled(true)
                } else {
                    micPermission.request()
                }
            }
        }
        item {
            QuickAction(
                Icons.Filled.Cast,
                stringResource(if (external.active) R.string.quick_capturing else R.string.source_other_apps),
                active = external.active,
            ) {
                if (external.active) {
                    viewModel.stopExternalAudio()
                } else {
                    capturePermissions.launch(
                        arrayOf(
                            android.Manifest.permission.RECORD_AUDIO,
                            android.Manifest.permission.POST_NOTIFICATIONS,
                        ),
                    )
                }
            }
        }
        item {
            QuickAction(
                Icons.Filled.Bedtime,
                stringResource(if (sleepRunning) R.string.quick_sleep_on else R.string.quick_sleep_30m),
                active = sleepRunning,
            ) {
                if (sleepRunning) viewModel.cancelSleepTimer() else viewModel.startSleepTimer(30)
            }
        }
        item {
            QuickAction(StoneIcon.SHUFFLE, stringResource(R.string.quick_shuffle_all), enabled = canShuffle) {
                viewModel.shuffleAllHistory()
            }
        }
    }
}

@Composable
private fun QuickAction(
    icon: StoneIcon,
    label: String,
    enabled: Boolean = true,
    active: Boolean = false,
    onClick: () -> Unit,
) = QuickActionShell(label, enabled, active, onClick) { StoneIconArt(icon, null, Modifier.size(22.dp), tint = it) }

@Composable
private fun QuickAction(
    icon: ImageVector,
    label: String,
    enabled: Boolean = true,
    active: Boolean = false,
    onClick: () -> Unit,
) = QuickActionShell(label, enabled, active, onClick) { Icon(icon, null, Modifier.size(22.dp), tint = it) }

@Composable
private fun QuickActionShell(
    label: String,
    enabled: Boolean,
    active: Boolean,
    onClick: () -> Unit,
    icon: @Composable (Color) -> Unit,
) {
    val tint =
        when {
            !enabled -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
            active -> MaterialTheme.colorScheme.primary
            else -> MaterialTheme.colorScheme.onSurface
        }
    val onLabel = stringResource(R.string.a11y_state_on)
    Column(
        Modifier
            // A minimum, not a fixed width, so "Shuffle all" at 200% font wraps instead of "Shu…".
            .widthIn(min = 84.dp, max = 120.dp)
            .selectedGlow(active, TILE_SHAPE)
            .clip(TILE_SHAPE)
            // On reads from the fill as well as the tint, not from colour alone; off is a step above
            // the card, so the tile has an edge.
            .background(if (active) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainerHighest)
            .semantics { if (active) stateDescription = onLabel }
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(vertical = GeodeSpacing.M, horizontal = GeodeSpacing.S),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
    ) {
        icon(tint)
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = tint,
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

internal fun formatClock(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    val hours = total / 3600
    val minutes = (total % 3600) / 60
    val seconds = total % 60
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, seconds) else "%d:%02d".format(minutes, seconds)
}

/** Settings › Look's "Show time left": what is left of the track after a minus sign; an unknown length reads 0:00. */
internal fun formatTimeLeft(
    positionMs: Long,
    durationMs: Long,
): String = if (durationMs <= 0) formatClock(0) else "\u2212" + formatClock(durationMs - positionMs)

/** A toggle, not a plain button, so TalkBack says whether shuffle is on rather than leaving it to the tint. */
@Composable
internal fun ShuffleButton(
    shuffle: Boolean,
    onToggle: () -> Unit,
) {
    // "On"/"Off" as the quick-action tiles say it, not the checkbox's "checked"/"not checked".
    val state = stringResource(if (shuffle) R.string.a11y_state_on else R.string.a11y_state_off)
    IconToggleButton(
        checked = shuffle,
        onCheckedChange = { onToggle() },
        modifier = Modifier.semantics { stateDescription = state },
        colors = IconButtonDefaults.iconToggleButtonColors(checkedContainerColor = MaterialTheme.colorScheme.secondaryContainer),
    ) {
        StoneIconArt(
            StoneIcon.SHUFFLE,
            stringResource(R.string.action_shuffle),
            tint = if (shuffle) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** Repeat-one gets its own icon, and the label names the mode, so all and one are told apart. */
@Composable
internal fun RepeatButton(
    repeatMode: Int,
    onClick: () -> Unit,
) {
    // On reads from the fill as well as the tint: the accent and the resting grey are close.
    val container = if (repeatMode != Player.REPEAT_MODE_OFF) MaterialTheme.colorScheme.secondaryContainer else Color.Transparent
    IconButton(onClick = onClick, colors = IconButtonDefaults.iconButtonColors(containerColor = container)) {
        StoneIconArt(
            if (repeatMode == Player.REPEAT_MODE_ONE) StoneIcon.REPEAT_ONE else StoneIcon.REPEAT,
            stringResource(
                when (repeatMode) {
                    Player.REPEAT_MODE_ALL -> R.string.action_repeat_all
                    Player.REPEAT_MODE_ONE -> R.string.action_repeat_one
                    else -> R.string.action_repeat_off
                },
            ),
            tint =
                if (repeatMode != Player.REPEAT_MODE_OFF) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
        )
    }
}
