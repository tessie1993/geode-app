package dev.geode.ui

import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.GuiPrefs
import dev.geode.model.TutorialChapter
import dev.geode.pro.PaywallTrigger
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.tour.GestureTipHost
import dev.geode.ui.tour.GestureTips
import dev.geode.ui.tour.TourChapters
import dev.geode.ui.tour.TourHint
import dev.geode.ui.tour.TourPointer
import dev.geode.ui.tour.TourSpot
import dev.geode.ui.tour.TourTargets
import dev.geode.ui.tour.spots
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map

/** Where the tour can send someone: the real screen a pointer is about. */
enum class TutorialTarget { PLAYER, LIBRARY, VISUALS, LIVE_VIEW, STUDIO_RENDER, STUDIO_EDIT, SETTINGS, PAYWALL }

@get:StringRes
val TutorialChapter.titleRes: Int
    get() =
        when (this) {
            TutorialChapter.WELCOME -> R.string.tutorial_chapter_welcome
            TutorialChapter.PLAYER -> R.string.tutorial_chapter_player
            TutorialChapter.LIBRARY -> R.string.tutorial_chapter_library
            TutorialChapter.VISUALS -> R.string.tutorial_chapter_visuals
            TutorialChapter.CUSTOMIZE -> R.string.tutorial_chapter_customize
            TutorialChapter.LIVE_VIEW -> R.string.tutorial_chapter_live_view
            TutorialChapter.STUDIO -> R.string.tutorial_chapter_studio
            TutorialChapter.ANYWHERE -> R.string.tutorial_chapter_anywhere
            TutorialChapter.PRO -> R.string.tutorial_chapter_pro
            TutorialChapter.SETTINGS -> R.string.tutorial_chapter_settings
        }

/** The screen a chapter's pointers live on, for a replay that has to take someone there first. */
internal val TutorialChapter.screen: TutorialTarget?
    get() =
        when (this) {
            TutorialChapter.PLAYER -> TutorialTarget.PLAYER
            TutorialChapter.LIBRARY -> TutorialTarget.LIBRARY
            TutorialChapter.VISUALS, TutorialChapter.CUSTOMIZE -> TutorialTarget.VISUALS
            TutorialChapter.STUDIO -> TutorialTarget.STUDIO_RENDER
            TutorialChapter.SETTINGS -> TutorialTarget.SETTINGS
            else -> null
        }

/**
 * The guided path's steps: light the Library tab, wait while the user plays a song, light the
 * Visuals tab. It runs once, after first run; it is the only part of the tour that leads.
 */
internal enum class GuideStage { OPEN_LIBRARY, PLAY_SONG, OPEN_VISUALS }

/**
 * What the tour is doing: the guided path's step ([guide]), and the screens whose pointers are
 * queued ([queue], the head on screen now, at pointer [step]).
 *
 * A screen's pointers queue themselves the first time that screen's controls are on screen
 * ([TutorialHost]); a replay from Settings › Tutorials queues the chapters it was given, in order,
 * and takes the user to each screen in turn ([replay]). Lives in [AppRoot] and is saved there, so
 * a rotation keeps the step.
 */
@Stable
class TutorialState internal constructor(
    queue: List<TutorialChapter>,
    step: Int,
    replay: Boolean,
    guide: GuideStage?,
    autoOfferUsed: Boolean,
) {
    var queue by mutableStateOf(queue)
        private set
    var step by mutableIntStateOf(step)
        internal set
    var replay by mutableStateOf(replay)
        private set
    internal var guide by mutableStateOf(guide)

    /**
     * Set once the guided path has been offered in this process, and by Reset all, so resetting
     * means "offer it on the next launch" rather than starting it over Settings.
     */
    var autoOfferUsed = autoOfferUsed

    val current: TutorialChapter? get() = queue.firstOrNull()

    val running: Boolean get() = guide != null || queue.isNotEmpty()

    /** Settings › Tutorials: these chapters, in tour order. Welcome is the guided path again. */
    fun start(chapters: List<TutorialChapter>) {
        val wanted = TourChapters.filter { it in chapters }
        guide = if (TutorialChapter.WELCOME in wanted) GuideStage.OPEN_LIBRARY else null
        queue = wanted - TutorialChapter.WELCOME
        step = 0
        replay = true
    }

    internal fun startGuide() {
        guide = GuideStage.OPEN_LIBRARY
        replay = false
    }

    internal fun startScreen(chapter: TutorialChapter) {
        queue = listOf(chapter)
        step = 0
        replay = false
    }

    internal fun nextChapter() {
        queue = queue.drop(1)
        step = 0
        if (queue.isEmpty() && guide == null) replay = false
    }

    fun end() {
        guide = null
        queue = emptyList()
        step = 0
        replay = false
    }

    companion object {
        val Saver: Saver<TutorialState, List<Any>> =
            Saver(
                save = {
                    listOf(it.queue.joinToString(",") { c -> c.name }, it.step, it.replay, it.guide?.name.orEmpty(), it.autoOfferUsed)
                },
                restore = {
                    TutorialState(
                        queue =
                            (it[0] as String)
                                .split(',')
                                .mapNotNull { n -> TutorialChapter.entries.firstOrNull { c -> c.name == n } },
                        step = it[1] as Int,
                        replay = it[2] as Boolean,
                        guide = GuideStage.entries.firstOrNull { g -> g.name == it[3] },
                        autoOfferUsed = it[4] as Boolean,
                    )
                },
            )
    }
}

@Composable
fun rememberTutorialState(): TutorialState =
    rememberSaveable(saver = TutorialState.Saver) {
        TutorialState(emptyList(), step = 0, replay = false, guide = null, autoOfferUsed = false)
    }

/**
 * The tour, drawn over the app: pointers on the real controls, never a carousel.
 *
 * - **The guided path** offers itself once, after first run: play a song, then see Visuals. Ending
 *   or skipping it sets [GuiPrefs.tutorialSeen] and ticks Welcome, then opens the one-time Pro
 *   offer ([PaywallTrigger.Onboarding]); a replay from Settings never does.
 * - **Each screen's pointers** (3 or 4) play the first time that screen's controls are on screen,
 *   once Welcome is ticked, and tick that chapter when finished or skipped. A screen whose
 *   controls carry no `tourTarget` yet never queues itself; a replay still plays its cards,
 *   centred.
 * - **Gesture tips** ([GestureTips]) show when nothing else does.
 *
 * Installs that skip onboarding without the guided path (the journey tests' `Onboarded` rule)
 * have no Welcome tick, so no pointer ever covers what they test. [blocked] holds everything back
 * while the safety notice, setup or the paywall is up.
 */
@Composable
internal fun TutorialHost(
    state: TutorialState,
    gui: GuiPrefs,
    blocked: Boolean,
    onGuiChange: (GuiPrefs) -> Unit,
    onShowMe: (TutorialTarget) -> Unit,
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { GestureTips.attach(context) }
    val player: PlayerViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val playingFlow = remember(player) { player.uiState.map { it.isPlaying }.distinctUntilChanged() }
    val playing by playingFlow.collectAsStateWithLifecycle(initialValue = false)
    val latestGui by rememberUpdatedState(gui)
    // One write per ending, from the newest prefs, so a tick never overwrites another.
    val tick: (TutorialChapter, Boolean) -> Unit = { chapter, seen ->
        val now = latestGui
        val progress = now.tutorialProgress
        onGuiChange(
            now.copy(
                tutorialSeen = now.tutorialSeen || seen,
                tutorialProgress = progress.copy(done = progress.done + chapter, resume = null),
            ),
        )
    }
    // The one-time Pro offer of a fresh install comes when its guided tour ends (finished or
    // skipped), once the person has heard music and seen the visuals; never after a replay.
    val endGuide = {
        val firstRun = !state.replay
        finishGuide(state, tick)
        if (firstRun) paywall.open(PaywallTrigger.Onboarding)
    }

    val offer = gui.setupDone && !gui.tutorialSeen
    LaunchedEffect(offer) {
        if (offer && !state.autoOfferUsed) {
            // A beat after setup, so the end-of-onboarding paywall is up first and the guide waits
            // behind it instead of flashing open for a frame.
            delay(OFFER_DELAY_MS)
            state.autoOfferUsed = true
            state.startGuide()
        }
    }
    val due by remember {
        derivedStateOf {
            val done = latestGui.tutorialProgress.done
            if (TutorialChapter.WELCOME !in done) {
                null
            } else {
                TourChapters.firstOrNull { ch -> ch !in done && ch.spots.any(TourTargets::isShown) }
            }
        }
    }
    LaunchedEffect(due, state.running) {
        val chapter = due ?: return@LaunchedEffect
        if (state.running) return@LaunchedEffect
        // Let the screen settle (a tab switch, a sheet rising) before pointing at it.
        delay(SETTLE_MS)
        if (!state.running && chapter.spots.any(TourTargets::isShown)) state.startScreen(chapter)
    }
    if (blocked) return

    when (state.guide) {
        GuideStage.OPEN_LIBRARY -> {
            LaunchedEffect(playing) { if (playing) state.guide = GuideStage.OPEN_VISUALS }
            GuidePointer(TourSpot.NAV_LIBRARY, 1, R.string.tour_guide_open_library, onSkip = endGuide) {
                onShowMe(TutorialTarget.LIBRARY)
                state.guide = GuideStage.PLAY_SONG
            }
        }
        GuideStage.PLAY_SONG -> {
            LaunchedEffect(playing) { if (playing) state.guide = GuideStage.OPEN_VISUALS }
            TourHint(
                text = stringResource(R.string.tour_guide_play_hint),
                actionLabel = stringResource(R.string.tour_skip_tour),
                onAction = endGuide,
            )
        }
        GuideStage.OPEN_VISUALS ->
            GuidePointer(TourSpot.NAV_VISUALS, 2, R.string.tour_guide_open_visuals, onSkip = endGuide) {
                onShowMe(TutorialTarget.VISUALS)
                endGuide()
            }
        null -> ScreenPointers(state, tick, onShowMe)
    }
}

/** One step of the guided path; its Skip (or Back) ends the whole path. */
@Composable
private fun GuidePointer(
    spot: TourSpot,
    index: Int,
    @StringRes primary: Int,
    onSkip: () -> Unit,
    onPrimary: () -> Unit,
) {
    TourPointer(
        title = stringResource(spot.titleRes),
        body = stringResource(spot.bodyRes),
        index = index,
        count = GUIDE_STEPS,
        target = TourTargets.boundsOf(spot),
        primaryLabel = stringResource(primary),
        onPrimary = onPrimary,
        skipLabel = stringResource(R.string.tour_skip_tour),
        onSkip = onSkip,
    )
}

private fun finishGuide(
    state: TutorialState,
    tick: (TutorialChapter, Boolean) -> Unit,
) {
    state.guide = null
    tick(TutorialChapter.WELCOME, true)
    if (state.queue.isEmpty()) state.end()
}

/** The head chapter's pointers, one at a time; tips when no chapter is queued. */
@Composable
private fun ScreenPointers(
    state: TutorialState,
    tick: (TutorialChapter, Boolean) -> Unit,
    onShowMe: (TutorialTarget) -> Unit,
) {
    val chapter = state.current
    if (chapter == null) {
        GestureTipHost()
        return
    }
    // A replay takes the user to the screen first and gives it a moment to lay out.
    var ready by remember(chapter, state.replay) { mutableStateOf(!state.replay) }
    LaunchedEffect(chapter, state.replay) {
        if (state.replay) {
            chapter.screen?.let(onShowMe)
            delay(SETTLE_MS)
            ready = true
        }
    }
    if (!ready) return
    // A first visit points only at what is on screen; a replay says every card.
    val steps = if (state.replay) chapter.spots else chapter.spots.filter(TourTargets::isShown)
    val finish = {
        tick(chapter, false)
        state.nextChapter()
    }
    if (steps.isEmpty()) {
        LaunchedEffect(chapter) { finish() }
        return
    }
    val index = state.step.coerceIn(0, steps.lastIndex)
    val spot = steps[index]
    val last = index == steps.lastIndex
    TourPointer(
        title = stringResource(spot.titleRes),
        body = stringResource(spot.bodyRes),
        index = index + 1,
        count = steps.size,
        target = TourTargets.boundsOf(spot),
        primaryLabel = stringResource(if (last) R.string.tour_done else R.string.tour_next),
        onPrimary = { if (last) finish() else state.step = index + 1 },
        skipLabel = stringResource(R.string.tour_skip),
        onSkip = finish,
    )
}

private const val OFFER_DELAY_MS = 600L
private const val SETTLE_MS = 700L
private const val GUIDE_STEPS = 2
