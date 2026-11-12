package dev.geode.ui.tour

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.data.AppStateRepository
import dev.geode.model.GestureTip
import dev.geode.model.Tour
import dev.geode.model.TourChapter
import dev.geode.model.TourCover
import dev.geode.model.TourFacts
import dev.geode.model.TourProgress
import dev.geode.model.TourRun
import dev.geode.model.TourSpot
import dev.geode.model.TourStep
import dev.geode.model.spots
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

/** A screen the tour asks the shell to open. */
enum class TourNav { LIBRARY, VISUALS, CUSTOMIZE, STUDIO }

/**
 * What the tour draws. [navigate], [offerPro] and [replayNote] are events kept as state: the host
 * acts on each and hands it back, so none is lost to a rotation or played twice.
 */
data class TourUiState(
    val step: TourStep? = null,
    val replay: Boolean = false,
    val tip: GestureTip? = null,
    val replayNote: Boolean = false,
    val navigate: TourNav? = null,
    val offerPro: Boolean = false,
    val progress: TourProgress = TourProgress(),
)

/**
 * The tour's one state holder. Progress is the repository's ([TourProgress] in the GUI prefs);
 * the run in hand lives here; the rules are [Tour]'s.
 *
 * The app's facts are read a beat late ([SETTLE_MS]): a tab switch or a sheet rising lays controls
 * out over several frames, and a step must not be skipped for a control that is about to appear.
 */
@OptIn(FlowPreview::class)
@HiltViewModel
class TourViewModel
    @Inject
    constructor(
        private val appState: AppStateRepository,
    ) : ViewModel() {
        private val run = MutableStateFlow<TourRun?>(null)
        private val facts = MutableStateFlow(TourFacts())
        private val cover = MutableStateFlow(TourCover(firstRun = true))

        /** False while a replay waits for the screen it asked for. */
        private val ready = MutableStateFlow(true)

        /**
         * No chapter starts by itself for the rest of this launch: after a Skip (not now means not
         * on the next screen either) and after Reset all (the tour greets the next launch).
         */
        private var quiet = false
        private var tipCounts = mapOf<GestureTip, Int>()
        private var settle: Job? = null

        private val _uiState = MutableStateFlow(TourUiState(progress = appState.value.value.tourProgress))
        val uiState: StateFlow<TourUiState> = _uiState.asStateFlow()

        init {
            viewModelScope.launch {
                appState.value
                    .map { it.tourProgress }
                    .distinctUntilChanged()
                    .collect { p -> _uiState.update { it.copy(progress = p, tip = it.tip.takeUnless { p.off }) } }
            }
            viewModelScope.launch {
                val saved = uiState.map { it.progress }.distinctUntilChanged()
                combine(run, facts.debounce(SETTLE_MS), cover, ready, saved) { run, facts, cover, ready, progress ->
                    Inputs(run, facts, !Tour.shows(run, progress, cover) || !ready, progress)
                }.collectLatest(::resolve)
            }
        }

        private data class Inputs(
            val run: TourRun?,
            val facts: TourFacts,
            val held: Boolean,
            val progress: TourProgress,
        )

        private suspend fun resolve(now: Inputs) {
            val current = now.run
            if (now.held) {
                show(null, current)
                return
            }
            if (current == null) {
                show(null, null)
                val due = Tour.due(now.progress, now.facts)?.takeUnless { quiet } ?: return
                if (due == TourChapter.START) delay(OFFER_DELAY_MS)
                run.value = Tour.resume(due, now.progress)
                return
            }
            // A step the user ended by doing what it asked counts as passed.
            val shown = _uiState.value.step?.spot
            if (shown != null && doneByUser(shown, current, now.facts)) {
                run.value = Tour.pass(current, shown)
                return
            }
            val step = Tour.step(current, now.facts)
            if (step == null) end(current, skipped = false) else show(step, current)
        }

        /** Whether [shown], the step on screen, is one [current] has not passed yet and the user just did. */
        private fun doneByUser(
            shown: TourSpot,
            current: TourRun,
            facts: TourFacts,
        ): Boolean = shown.chapter == current.chapter && current.at <= shown.ordinalInChapter && Tour.reached(shown, facts)

        private fun show(
            step: TourStep?,
            run: TourRun?,
        ) {
            _uiState.update { it.copy(step = step, replay = run?.replay == true) }
            if (step != null && run?.replay == false && _uiState.value.progress.resume != step.spot) {
                save { it.copy(resume = step.spot) }
            }
        }

        /** The host's view of the app, on every change. */
        fun onFacts(now: TourFacts) {
            facts.value = now
        }

        /** What is over the app now; while anything is ([Tour.held]) no step or tip shows. */
        fun onCover(now: TourCover) {
            cover.value = now
        }

        /** Next, Done, or the step's own action. */
        fun onPrimary() {
            val step = _uiState.value.step ?: return
            val current = run.value ?: return
            when (step.spot) {
                TourSpot.NAV_LIBRARY -> _uiState.update { it.copy(navigate = TourNav.LIBRARY) }
                TourSpot.NAV_VISUALS -> _uiState.update { it.copy(navigate = TourNav.VISUALS) }
                else -> Unit
            }
            run.value = Tour.pass(current, step.spot)
        }

        /** Skip, or Back: ends the run. Skipping the first-run chapter turns the tour off. */
        fun onSkip() {
            run.value?.let { end(it, skipped = true) }
        }

        fun onNavigated() {
            _uiState.update { it.copy(navigate = null) }
        }

        fun onProOffered() {
            _uiState.update { it.copy(offerPro = false) }
        }

        fun onReplayNoteShown() {
            _uiState.update { it.copy(replayNote = false) }
        }

        /** Settings › Tutorials: plays [chapters] in tour order, taking the user to each screen. */
        fun replay(chapters: Collection<TourChapter>) {
            start(Tour.replay(chapters))
        }

        /** Settings › Tutorials › Reset all. */
        fun reset() {
            quiet = true
            tipCounts = emptyMap()
            start(null)
            _uiState.update { it.copy(tip = null) }
            save { TourProgress() }
        }

        /** A control used the long way round; its gesture is mentioned once, when no tour runs. */
        fun nudge(tip: GestureTip) {
            val state = _uiState.value
            if (!Tour.mayTip(tip, state.progress, cover.value, run.value)) return
            val n = (tipCounts[tip] ?: 0) + 1
            tipCounts = tipCounts + (tip to n)
            if (n >= TIP_AFTER && state.tip == null) _uiState.update { it.copy(tip = tip) }
        }

        fun onTipRead() {
            val tip = _uiState.value.tip ?: return
            _uiState.update { it.copy(tip = null) }
            save { it.copy(tips = it.tips + tip) }
        }

        /** The tip's "Don't show tips": no more gesture tips or chapters until Settings › Tutorials turns them on. */
        fun onTipsOff() {
            val tip = _uiState.value.tip
            _uiState.update { it.copy(tip = null) }
            save { it.copy(tips = it.tips + listOfNotNull(tip), off = true) }
        }

        private fun end(
            ended: TourRun,
            skipped: Boolean,
        ) {
            if (skipped) quiet = true
            save { Tour.ended(it, ended, skipped) }
            _uiState.update {
                it.copy(
                    offerPro = it.offerPro || Tour.offersPro(ended, skipped),
                    replayNote = it.replayNote || (skipped && !ended.replay),
                )
            }
            start(if (skipped) null else Tour.nextChapter(ended))
        }

        private fun start(next: TourRun?) {
            settle?.cancel()
            val nav = next?.takeIf { it.replay }?.chapter?.screen
            ready.value = nav == null
            // A screen not yet opened stays asked for: the first run's last step (Open Visuals) asks
            // for Visuals and its chapter ends at once, before the host has read the request.
            _uiState.update { it.copy(step = null, tip = if (next == null) it.tip else null, navigate = nav ?: it.navigate) }
            run.value = next
            if (nav != null) {
                settle =
                    viewModelScope.launch {
                        delay(REPLAY_SETTLE_MS)
                        ready.value = true
                    }
            }
        }

        private fun save(change: (TourProgress) -> TourProgress) {
            val before = _uiState.value.progress
            val next = change(before)
            if (next == before) return
            _uiState.update { it.copy(progress = next) }
            // Applied to the stored value, not [next]: Settings › Tutorials writes the tips switch
            // straight to the repository, and a save racing it must not undo it.
            viewModelScope.launch { appState.update { it.copy(tourProgress = change(it.tourProgress)) } }
        }

        private val TourSpot.ordinalInChapter: Int get() = chapter.spots.indexOf(this)

        private val TourChapter.screen: TourNav?
            get() =
                when (this) {
                    TourChapter.START -> null
                    TourChapter.VISUALS -> TourNav.VISUALS
                    TourChapter.CUSTOMIZE -> TourNav.CUSTOMIZE
                    TourChapter.STUDIO -> TourNav.STUDIO
                }

        internal companion object {
            const val SETTLE_MS = 400L
            const val OFFER_DELAY_MS = 600L
            const val REPLAY_SETTLE_MS = 1_000L
            private const val TIP_AFTER = 1
        }
    }
