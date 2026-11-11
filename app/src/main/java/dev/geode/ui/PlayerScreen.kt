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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
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
import androidx.compose.ui.layout.SubcomposeLayout
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

    val header: @Composable () -> Unit = {
        Row(
            readable.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 16.dp),
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
    val hero: @Composable (Dp) -> Unit = { artMax ->
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
            artMax = artMax,
            modifier = readable.padding(horizontal = 16.dp),
        )
    }
    val transport: @Composable () -> Unit = {
        TransportCard(
            viewModel = viewModel,
            state = state,
            position = { position.value },
            abLoop = abLoop,
            modifier = readable.padding(horizontal = 16.dp),
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
                    modifier = readable.padding(horizontal = 16.dp),
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
                modifier = readable.padding(horizontal = 16.dp),
            )
        }
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
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
        // The dock's height over this pane's bottom (AppShell), padded inside the scroll.
        val dockReserve = LocalDockReserve.current
        if (sideBySide) {
            Row(Modifier.fillMaxSize()) {
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .verticalScroll(rememberScrollState())
                        .padding(bottom = 24.dp + dockReserve),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    header()
                    if (transportWithArt) {
                        HeroOverTransport(paneHeight = paneHeight - dockReserve, hero = hero, transport = transport)
                    } else {
                        hero((paneHeight - dockReserve - HERO_CHROME).coerceIn(MIN_ART, MAX_ART))
                    }
                }
                LazyColumn(
                    Modifier.weight(1f).fillMaxHeight(),
                    contentPadding = PaddingValues(top = 16.dp, bottom = 24.dp + dockReserve),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (!transportWithArt) item { transport() }
                    cards()
                }
            }
        } else {
            // The art takes what the pane leaves above the dock once the transport is measured, as
            // in the side-by-side branch, so the transport opens above the fold instead of half
            // under the tab bar; the list itself scrolls under the glass.
            LazyColumn(
                Modifier.fillMaxSize(),
                contentPadding = PaddingValues(bottom = 24.dp + dockReserve),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                item { header() }
                item { HeroOverTransport(paneHeight = paneHeight - dockReserve, hero = hero, transport = transport) }
                cards()
            }
        }
    }
    if (queueSheet) {
        QueueSheet(viewModel, queue, favourites, onDismiss = { queueSheet = false })
    }
}

/**
 * The side-by-side hero with the transport under it, in one measure pass: the transport is
 * measured first, then the hero is composed with the art size what is left of [paneHeight]
 * under [HERO_CHROME] and the transport, so at a large font the art shrinks while the transport
 * grows instead of pushing it off the pane (#558; Compose's scalable-content guide: content
 * reflows to fit its container, Context7 `/websites/developer_android_develop_ui_compose`).
 *
 * WHY measure here and not `onSizeChanged`: writing the transport's height to state in the
 * layout phase and reading it in composition is a backwards write. The first frame drew the art
 * at the floor's size and the second popped it to the measured one (compose-performance skill,
 * references/diagnosis.md axis 3, "`onSizeChanged` (layout phase) writing state read by a
 * sibling in composition"; jetpack-compose-performance skill, references/layouts.md § "Prohibit
 * layout feedback loops"; Android docs, kb://android/develop/ui/compose/performance/backwards-write).
 * Here the height flows forward inside one layout pass, so the first frame is already right.
 *
 * WHY a subcomposition and not a plain `Layout` with the art at `Constraints.fixed(side, side)`:
 * the art is not a sibling of the transport but the top of the hero card, under its padding and
 * above its title rows, and its cap is a composition parameter of [PlayerHero]. A plain `Layout`
 * could only hand that cap down through state written mid-measure, which is the same backwards
 * flow one phase later, or by changing the card's width, which changes the look. Subcomposing
 * the hero once the transport is measured is what `SubcomposeLayout` is for. Its cost is the
 * one the layouts reference warns of in lazy items and hot paths; this sits once per pane (a
 * scrolling `Column`, or the single top item of the portrait list, never a repeated row), and it
 * recomposes only when the pane or the transport's height changes the art's size.
 *
 * The look at font scale 1.0 is unchanged: the same [MIN_ART]..[MAX_ART] clamp, the same
 * [TRANSPORT_GAP] the column's `spacedBy` used to put between them, and [TRANSPORT_HEIGHT] as
 * the floor the transport's measured height only takes over from when it needs more.
 */
@Composable
private fun HeroOverTransport(
    paneHeight: Dp,
    hero: @Composable (Dp) -> Unit,
    transport: @Composable () -> Unit,
) {
    SubcomposeLayout { constraints ->
        // The column scrolls, so the height is unbounded; only the width is the column's.
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val transportPlaceable = subcompose(HeroSlot.TRANSPORT, transport).single().measure(loose)
        val gap = TRANSPORT_GAP.roundToPx()
        val transportRoom = maxOf(transportPlaceable.height + gap, TRANSPORT_HEIGHT.roundToPx())
        val side = (paneHeight - HERO_CHROME - transportRoom.toDp()).coerceIn(MIN_ART, MAX_ART)
        val heroPlaceable = subcompose(HeroSlot.HERO) { hero(side) }.single().measure(loose)
        val width =
            maxOf(heroPlaceable.width, transportPlaceable.width)
                .coerceIn(constraints.minWidth, constraints.maxWidth)
        val height = heroPlaceable.height + gap + transportPlaceable.height
        layout(width, height) {
            heroPlaceable.placeRelative((width - heroPlaceable.width) / 2, 0)
            transportPlaceable.placeRelative((width - transportPlaceable.width) / 2, heroPlaceable.height + gap)
        }
    }
}

/** The two slots of [HeroOverTransport]'s subcomposition. */
private enum class HeroSlot { TRANSPORT, HERO }

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

/**
 * The least room the art leaves for the transport card (seek row, buttons, A–B and Auto) under
 * it, gap included. WHY a floor and not the answer: it was the whole answer until #558, and the
 * layout at font scale 1.0 was tuned against it, so the card's measured height only takes over
 * when it needs more (a large font, a long translation), and today's look is kept at 1.0.
 */
private val TRANSPORT_HEIGHT = 180.dp

/** The gap between the hero and the transport in [HeroOverTransport]: the column's `spacedBy` gap. */
private val TRANSPORT_GAP = 16.dp

/** On a phone the art fills the width; on anything wider it stops here. */
private val MAX_ART = 420.dp
private val MIN_ART = 160.dp

/** Header, title rows, chips and buttons around the art in the side-by-side hero. */
private val HERO_CHROME = 300.dp

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
    artMax: Dp,
    modifier: Modifier = Modifier,
) {
    val uri = remember(state.title, state.artist) { viewModel.currentTrackUri() }
    val foreign = external.active
    val foreignTrack = external.nowPlaying?.takeIf { it.title.isNotBlank() }
    val hasSource = foreign || micActive || state.hasMedia
    val isFavourite = uri != null && uri in favourites
    Column(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(24.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .clickable(enabled = hasSource, onClick = onExpand)
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Square and as wide as the card on a phone, but never taller than [artMax]: on a tablet a
        // full-width square pushed the title and transport two screens down.
        TrackArtwork(
            if (foreign || micActive) null else uri,
            Modifier
                .align(Alignment.CenterHorizontally)
                .widthIn(max = artMax)
                .fillMaxWidth()
                .aspectRatio(1f),
            corner = 18.dp,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    when {
                        foreign -> external.nowPlaying?.appLabel ?: stringResource(R.string.source_other_apps)
                        micActive -> stringResource(R.string.source_live_input)
                        state.isPlaying -> stringResource(R.string.state_now_playing)
                        state.hasMedia -> stringResource(R.string.state_paused)
                        else -> stringResource(R.string.state_nothing_playing)
                    },
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    when {
                        foreign -> foreignTrack?.title ?: stringResource(R.string.title_whatever_is_playing)
                        micActive -> stringResource(R.string.title_the_room)
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
                        foreign ->
                            foreignTrack?.artist?.ifBlank { null }
                                ?: stringResource(R.string.subtitle_captured_from_another_app)
                        micActive -> stringResource(R.string.subtitle_microphone_hears)
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
            if (state.hasMedia && !foreign) {
                IconButton(onClick = { viewModel.toggleFavourite() }) {
                    StoneIconArt(
                        StoneIcon.FAVORITE,
                        stringResource(
                            if (isFavourite) R.string.action_favourite_remove else R.string.action_favourite_add,
                        ),
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
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            SceneChip(styleLabel)
            Box(Modifier.weight(1f))
            if (foreign) {
                FilledTonalButton(
                    onClick = viewModel::stopExternalAudio,
                    contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
                ) {
                    Text(stringResource(R.string.action_stop_capture))
                }
            }
        }
        if (!hasSource) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = viewModel::resumeLastPlayed, enabled = canResume) {
                    Text(stringResource(R.string.action_resume_last_played))
                }
                FilledTonalButton(onClick = onOpenLibrary) { Text(stringResource(R.string.action_open_library)) }
            }
        }
    }
}

@Composable
private fun SceneChip(label: String) {
    Box(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .padding(horizontal = 10.dp, vertical = 4.dp),
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
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(24.dp))
            .background(MaterialTheme.colorScheme.surfaceContainer)
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            PositionClock(position)
            TrackSeekBar(
                positionMs = position,
                durationMs = state.durationMs,
                loopStartMs = abLoop?.startMs,
                loopEndMs = abLoop?.endMs,
                onSeek = viewModel::seekTo,
                modifier = Modifier.weight(1f).height(40.dp),
            )
            Text(formatClock(state.durationMs), style = MaterialTheme.typography.labelSmall)
        }
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceEvenly,
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
        // The queue's show/hide toggle that sat here is the Up next card's header now.
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceEvenly,
        ) {
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
            .clip(RoundedCornerShape(20.dp))
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
                .padding(start = 14.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(
                summary,
                Modifier.weight(1f).padding(horizontal = 12.dp),
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
            Modifier.fillMaxWidth().padding(start = 14.dp, end = 14.dp, bottom = 4.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            val untitled = stringResource(R.string.title_untitled)
            upNext.forEach { t ->
                Text(
                    t.title.ifBlank { untitled },
                    Modifier.fillMaxWidth().clickable(onClick = onOpenQueue),
                    style = MaterialTheme.typography.bodyMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
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
            stringResource(R.string.source_other_apps).takeIf { external.supported },
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
            contentPadding = PaddingValues(start = 14.dp, end = 14.dp, bottom = 14.dp),
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
        horizontalArrangement = Arrangement.spacedBy(10.dp),
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
        if (external.supported) {
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
            .selectedGlow(active, RoundedCornerShape(18.dp))
            .clip(RoundedCornerShape(18.dp))
            // On reads from the fill as well as the tint, not from colour alone.
            .background(if (active) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainer)
            .semantics { if (active) stateDescription = onLabel }
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(vertical = 12.dp, horizontal = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
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
    IconButton(onClick = onClick) {
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
