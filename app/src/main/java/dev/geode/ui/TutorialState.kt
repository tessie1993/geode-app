package dev.geode.ui

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.collectIsDraggedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.geode.R
import dev.geode.data.GuiPrefs
import dev.geode.model.TutorialChapter
import dev.geode.model.TutorialPosition
import dev.geode.model.TutorialTour
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalReducedMotion
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.UiSound
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Whether a tour is running, which page it is on, and whether it is open or paused behind a
 * Resume pill (after Show me or Back).
 *
 * Lives in [AppRoot] and is saved there, so a rotation or a trip to the real screen keeps the
 * page. [firstRun] marks the tour that offers itself after setup: only that one writes a resume
 * point and, once finished or skipped, [GuiPrefs.tutorialSeen]. A replay from Settings ticks
 * chapters off but otherwise leaves history alone.
 */
@Stable
class TutorialState internal constructor(
    chapters: List<TutorialChapter>,
    page: Int,
    open: Boolean,
    firstRun: Boolean,
    autoOfferUsed: Boolean,
) {
    var tour by mutableStateOf(chapters.takeIf { it.isNotEmpty() }?.let(::TutorialTour))
        private set
    var page by mutableIntStateOf(page)
    var open by mutableStateOf(open)
        private set
    var firstRun by mutableStateOf(firstRun)
        private set

    /**
     * Set once the first-run offer has been made in this process, and by Reset all, so resetting
     * means "offer it on the next launch" rather than throwing the tour up over Settings.
     */
    var autoOfferUsed = autoOfferUsed

    fun start(
        chapters: List<TutorialChapter>,
        firstRun: Boolean = false,
        at: TutorialPosition? = null,
    ) {
        val next = TutorialTour(chapters)
        tour = next
        page = at?.let(next::pageOf) ?: 0
        this.firstRun = firstRun
        open = true
    }

    fun pause() {
        open = false
    }

    fun resume() {
        open = tour != null
    }

    fun end() {
        tour = null
        open = false
    }

    companion object {
        val Saver: Saver<TutorialState, List<Any>> =
            Saver(
                save = {
                    val chapters = it.tour?.chapters.orEmpty().joinToString(",") { c -> c.name }
                    listOf(chapters, it.page, it.open, it.firstRun, it.autoOfferUsed)
                },
                restore = {
                    TutorialState(
                        chapters =
                            (it[0] as String).split(',').mapNotNull { n -> TutorialChapter.entries.firstOrNull { c -> c.name == n } },
                        page = it[1] as Int,
                        open = it[2] as Boolean,
                        firstRun = it[3] as Boolean,
                        autoOfferUsed = it[4] as Boolean,
                    )
                },
            )
    }
}

@Composable
fun rememberTutorialState(): TutorialState =
    rememberSaveable(saver = TutorialState.Saver) {
        TutorialState(emptyList(), page = 0, open = false, firstRun = false, autoOfferUsed = false)
    }

/**
 * The tour, drawn over the app: the carousel when open, the Resume pill when paused.
 *
 * The carousel is a full-screen [Dialog], not a layer in the shell's window, so TalkBack cannot
 * wander into the app underneath and system Back reaches it first. [blocked] holds it back while
 * the safety notice, setup or the paywall is up; the tour waits rather than stacking on them.
 */
@Composable
internal fun TutorialHost(
    state: TutorialState,
    gui: GuiPrefs,
    blocked: Boolean,
    onGuiChange: (GuiPrefs) -> Unit,
    onShowMe: (TutorialTarget) -> Unit,
) {
    // Offered once, to someone who has finished setup and has not finished or skipped the tour,
    // picking up where they left off if the app was closed mid-tour.
    val offer = gui.setupDone && !gui.tutorialSeen
    LaunchedEffect(offer) {
        if (offer && !state.autoOfferUsed) {
            // A beat after setup, so the end-of-onboarding paywall is up first and the tour waits
            // behind it instead of flashing open for a frame.
            delay(OFFER_DELAY_MS)
            state.autoOfferUsed = true
            state.start(TutorialChapter.entries, firstRun = true, at = gui.tutorialProgress.resume)
        }
    }
    val tour = state.tour ?: return
    if (blocked) return
    // One write for the whole ending, so the last chapter's tick and "seen" cannot race each other.
    val endTour = { finished: TutorialChapter? ->
        val progress = gui.tutorialProgress
        val done = progress.done + listOfNotNull(finished)
        when {
            state.firstRun -> onGuiChange(gui.copy(tutorialSeen = true, tutorialProgress = progress.copy(done = done, resume = null)))
            finished != null -> onGuiChange(gui.copy(tutorialProgress = progress.copy(done = done)))
        }
        state.end()
    }
    if (!state.open) {
        TutorialResumePill(onResume = state::resume, onEnd = { endTour(null) })
        return
    }
    Dialog(
        onDismissRequest = state::pause,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false, dismissOnClickOutside = false),
    ) {
        TutorialCarousel(
            tour = tour,
            startPage = state.page,
            onMove = { from, to ->
                val done = tour.completedBy(from, to)
                state.page = to
                val progress = gui.tutorialProgress
                val next =
                    progress.copy(
                        done = progress.done + listOfNotNull(done),
                        resume = if (state.firstRun) tour.positionOf(state.page) else progress.resume,
                    )
                if (next != progress) onGuiChange(gui.copy(tutorialProgress = next))
                done
            },
            onFinish = { endTour(tour.completedBy(tour.pageCount - 1, tour.pageCount)) },
            onSkipAll = { endTour(null) },
            onShowMe = { target ->
                state.pause()
                onShowMe(target)
            },
        )
    }
}

/**
 * The carousel itself, layout "B": a big animated drawing on top, and under it a glass card in
 * which Wave says the card's line.
 *
 * [onMove] is told every settled page change and answers with the chapter that move finished,
 * if any, which is the cue to celebrate.
 */
@Composable
internal fun TutorialCarousel(
    tour: TutorialTour,
    startPage: Int,
    onMove: (from: Int, to: Int) -> TutorialChapter?,
    onFinish: () -> Unit,
    onSkipAll: () -> Unit,
    onShowMe: (TutorialTarget) -> Unit,
) {
    val reducedMotion = LocalReducedMotion.current
    val sounds = LocalUiSounds.current
    val view = LocalView.current
    val scope = rememberCoroutineScope()
    // The collector below outlives compositions; it must call the latest callback, not the first.
    val currentOnMove by rememberUpdatedState(onMove)
    val pager = rememberPagerState(initialPage = startPage.coerceIn(0, tour.pageCount - 1)) { tour.pageCount }
    val confetti = remember { Animatable(0f) }
    var cheering by remember { mutableStateOf<TutorialChapter?>(null) }
    var talking by remember { mutableStateOf(true) }

    val celebrate: (TutorialChapter?) -> Unit = { chapter ->
        if (chapter != null) {
            sounds.play(UiSound.CONFIRM)
            view.performStoneHaptic(StoneHapticCue.CONFIRM)
            cheering = chapter
            if (!reducedMotion) {
                scope.launch {
                    confetti.snapTo(0f)
                    confetti.animateTo(1f, tween(1100))
                }
            }
        }
    }
    // A swipe gets the swoop; a button press already made its own sound.
    val dragged by pager.interactionSource.collectIsDraggedAsState()
    var swiped by remember { mutableStateOf(false) }
    LaunchedEffect(dragged) { if (dragged) swiped = true }
    LaunchedEffect(pager) {
        var last = pager.settledPage
        snapshotFlow { pager.settledPage }.collect { now ->
            if (now == last) return@collect
            if (swiped) sounds.play(UiSound.SWOOP)
            swiped = false
            view.performStoneHaptic(StoneHapticCue.TAP)
            celebrate(currentOnMove(last, now))
            last = now
        }
    }
    LaunchedEffect(pager.settledPage) {
        talking = true
        delay(1600)
        talking = false
    }
    LaunchedEffect(cheering) {
        if (cheering != null) {
            delay(1400)
            cheering = null
        }
    }

    val go: (Int) -> Unit = { target ->
        scope.launch { if (reducedMotion) pager.scrollToPage(target) else pager.animateScrollToPage(target) }
    }
    val page = pager.currentPage
    val position = tour.positionOf(page)
    val card = position.chapter.cardContent[position.card]
    val lastPage = page == tour.pageCount - 1

    Box(
        Modifier
            .fillMaxSize()
            .background(GeodeColors.Canvas.copy(alpha = 0.94f))
            .testTag(TUTORIAL_TAG),
    ) {
        Column(
            Modifier
                .align(Alignment.Center)
                .systemBarsPadding()
                .padding(GeodeSpacing.L)
                // A reading width on a tablet rather than a 2,500px-wide strip.
                .widthIn(max = 560.dp)
                .fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                tour.nextChapterPage(page)?.let { next ->
                    CrystalButton(onClick = { go(next) }, filled = false) { Text(stringResource(R.string.tutorial_skip_chapter)) }
                }
                Spacer(Modifier.weight(1f))
                CrystalButton(onClick = onSkipAll, filled = false) { Text(stringResource(R.string.tutorial_skip_all)) }
            }
            HorizontalPager(pager, Modifier.weight(1f).fillMaxWidth()) { p ->
                val at = tour.positionOf(p)
                TutorialPage(
                    card = at.chapter.cardContent[at.card],
                    talking = talking && p == pager.settledPage,
                    cheer = cheering != null && p == pager.settledPage,
                    cheerChapter = cheering,
                )
            }
            CrystalOverline(
                stringResource(
                    R.string.tutorial_progress,
                    stringResource(position.chapter.titleRes),
                    position.card + 1,
                    position.chapter.cards,
                ),
                Modifier.align(Alignment.CenterHorizontally),
            )
            ChapterDots(count = position.chapter.cards, current = position.card)
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                card.showMe?.let { target ->
                    CrystalButton(onClick = { onShowMe(target) }, filled = false) { Text(stringResource(R.string.tutorial_show_me)) }
                }
                Spacer(Modifier.weight(1f))
                if (page > 0) {
                    CrystalButton(onClick = { go(page - 1) }, filled = false) { Text(stringResource(R.string.tutorial_back)) }
                    Spacer(Modifier.size(GeodeSpacing.S))
                }
                CrystalButton(
                    onClick = { if (lastPage) onFinish() else go(page + 1) },
                ) { Text(stringResource(if (lastPage) R.string.tutorial_finish else R.string.tutorial_next)) }
            }
        }
        Canvas(Modifier.fillMaxSize()) { drawConfettiBurst(confetti.value) }
    }
}

@Composable
private fun TutorialPage(
    card: TutorialCard,
    talking: Boolean,
    cheer: Boolean,
    cheerChapter: TutorialChapter?,
) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        // Side by side on a short, wide window (a phone on its side), stacked everywhere else.
        if (maxWidth > maxHeight) {
            Row(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.L), verticalAlignment = Alignment.CenterVertically) {
                TutorialArtwork(card.art, Modifier.weight(1f).fillMaxHeight())
                TutorialSpeech(card, talking, cheer, cheerChapter, Modifier.weight(1f))
            }
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(GeodeSpacing.M)) {
                TutorialArtwork(card.art, Modifier.weight(1f).fillMaxWidth())
                TutorialSpeech(card, talking, cheer, cheerChapter, Modifier.fillMaxWidth())
            }
        }
    }
}

/** Wave and the card's words, read by TalkBack as one announcement each time the card changes. */
@Composable
private fun TutorialSpeech(
    card: TutorialCard,
    talking: Boolean,
    cheer: Boolean,
    cheerChapter: TutorialChapter?,
    modifier: Modifier = Modifier,
) {
    val cs = MaterialTheme.colorScheme
    Row(
        modifier
            .crystalPanel(0.92f, cs.surface, cs.primary, corner = 20.dp, glowStrength = 0.7f)
            .padding(GeodeSpacing.L)
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
    ) {
        WaveMascot(talking = talking, cheer = cheer, modifier = Modifier.size(56.dp))
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                Text(
                    stringResource(card.titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    color = cs.onSurface,
                    modifier = Modifier.weight(1f, fill = false).semantics { heading() },
                )
                if (card.pro) ProBadge()
            }
            Text(stringResource(card.bodyRes), style = MaterialTheme.typography.bodyMedium, color = cs.onSurfaceVariant)
            cheerChapter?.takeIf { cheer }?.let {
                Text(
                    stringResource(R.string.tutorial_chapter_done, stringResource(it.titleRes)),
                    style = MaterialTheme.typography.labelLarge,
                    color = cs.primary,
                )
            }
        }
    }
}

@Composable
private fun ChapterDots(
    count: Int,
    current: Int,
) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally)) {
        repeat(count) { i ->
            Box(
                Modifier
                    .size(if (i == current) 10.dp else 7.dp)
                    .clip(CircleShape)
                    .background(if (i == current) MaterialTheme.colorScheme.primary else Color.White.copy(alpha = 0.25f)),
            )
        }
    }
}

/** Wave waiting at the top of the screen while someone looks at the real thing after Show me. */
@Composable
private fun TutorialResumePill(
    onResume: () -> Unit,
    onEnd: () -> Unit,
) {
    Box(Modifier.fillMaxSize().statusBarsPadding().padding(GeodeSpacing.S)) {
        Row(
            Modifier
                .align(Alignment.TopEnd)
                .crystalPanel(0.92f, MaterialTheme.colorScheme.surface, MaterialTheme.colorScheme.primary, corner = 28.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CrystalButton(onClick = onResume, filled = false) {
                WaveMascot(talking = true, modifier = Modifier.size(28.dp))
                Text(stringResource(R.string.tutorial_resume))
            }
            CrystalButton(onClick = onEnd, filled = false) { Text(stringResource(R.string.tutorial_skip_all)) }
        }
    }
}

internal const val TUTORIAL_TAG = "tutorial-carousel"
private const val OFFER_DELAY_MS = 600L
