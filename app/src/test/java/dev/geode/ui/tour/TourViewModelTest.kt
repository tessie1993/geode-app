package dev.geode.ui.tour

import dev.geode.data.GuiPrefs
import dev.geode.data.UserDataRepository
import dev.geode.model.GestureTip
import dev.geode.model.TourChapter
import dev.geode.model.TourFacts
import dev.geode.model.TourPlace
import dev.geode.model.TourProgress
import dev.geode.model.TourSpot
import dev.geode.model.spots
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** The tour's state holder against a fake repository: what shows, what is saved, what is asked once. */
@OptIn(ExperimentalCoroutinesApi::class)
class TourViewModelTest {
    private val main = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(main)

    @After
    fun tearDown() = Dispatchers.resetMain()

    private class FakeUserData(
        progress: TourProgress,
    ) : UserDataRepository {
        override val guiPrefs = MutableStateFlow(GuiPrefs(tourProgress = progress))
        override val loaded = MutableStateFlow(true)

        override suspend fun setGuiPrefs(prefs: GuiPrefs) {
            guiPrefs.value = prefs
        }

        val progress: TourProgress get() = guiPrefs.value.tourProgress
    }

    private val tabs = setOf(TourSpot.NAV_LIBRARY, TourSpot.NAV_VISUALS)
    private val onLibrary = TourFacts(shown = tabs, place = TourPlace.LIBRARY)
    private val onVisuals = TourFacts(shown = tabs + TourChapter.VISUALS.spots, place = TourPlace.VISUALS, playing = true)
    private val startDone = TourProgress(done = setOf(TourChapter.START))

    private fun TestScope.tour(
        data: FakeUserData,
        facts: TourFacts,
    ): TourViewModel =
        TourViewModel(data).also {
            it.onBlocked(false)
            it.onFacts(facts)
            settle()
        }

    private fun TestScope.settle() = advanceTimeBy(SETTLE)

    @Test
    fun `a new install is led from a song to Visuals, then shown Visuals`() =
        runTest(main) {
            val data = FakeUserData(TourProgress())
            val vm = tour(data, onLibrary)
            assertEquals(TourSpot.PLAY_SONG, vm.uiState.value.step?.spot)
            assertEquals(TourSpot.PLAY_SONG, data.progress.resume)

            vm.onFacts(onLibrary.copy(playing = true))
            settle()
            assertEquals(TourSpot.NAV_VISUALS, vm.uiState.value.step?.spot)

            vm.onPrimary()
            settle()
            assertEquals(TourNav.VISUALS, vm.uiState.value.navigate)
            vm.onNavigated()
            assertEquals(setOf(TourChapter.START), data.progress.done)
            assertFalse(vm.uiState.value.offerPro)

            vm.onFacts(onVisuals)
            settle()
            val step = vm.uiState.value.step!!
            assertEquals(TourSpot.VISUALS_TABS, step.spot)
            assertEquals(4, step.count)
        }

    @Test
    fun `opening Visuals by hand ends the first run too`() =
        runTest(main) {
            val data = FakeUserData(TourProgress())
            val vm = tour(data, onLibrary)
            vm.onFacts(onVisuals)
            settle()
            assertEquals(setOf(TourChapter.START), data.progress.done)
            assertEquals(TourSpot.VISUALS_TABS, vm.uiState.value.step?.spot)
        }

    @Test
    fun `a chapter with one control on screen is one step, then the Pro offer`() =
        runTest(main) {
            val data = FakeUserData(startDone)
            val vm = tour(data, onVisuals.copy(shown = setOf(TourSpot.STAGE_RECORD)))
            val step = vm.uiState.value.step!!
            assertEquals(TourSpot.STAGE_RECORD, step.spot)
            assertTrue(step.last)

            vm.onPrimary()
            settle()
            assertNull(vm.uiState.value.step)
            assertEquals(setOf(TourChapter.START, TourChapter.VISUALS), data.progress.done)
            assertNull(data.progress.resume)
            assertTrue(vm.uiState.value.offerPro)
            vm.onProOffered()
            assertFalse(vm.uiState.value.offerPro)
        }

    @Test
    fun `a control that leaves mid-step is passed over`() =
        runTest(main) {
            val data = FakeUserData(startDone)
            val vm = tour(data, onVisuals)
            vm.onFacts(onVisuals.copy(shown = setOf(TourSpot.STAGE_TOOLS)))
            settle()
            assertEquals(TourSpot.STAGE_TOOLS, vm.uiState.value.step?.spot)
            vm.onFacts(onVisuals.copy(shown = emptySet()))
            settle()
            assertNull(vm.uiState.value.step)
            assertTrue(TourChapter.VISUALS in data.progress.done)
        }

    @Test
    fun `skipping the first run turns the tour off and says where to find it`() =
        runTest(main) {
            val data = FakeUserData(TourProgress())
            val vm = tour(data, onLibrary)
            vm.onSkip()
            settle()
            assertNull(vm.uiState.value.step)
            assertEquals(TourProgress.Finished, data.progress)
            assertTrue(vm.uiState.value.replayNote)
            assertTrue(vm.uiState.value.offerPro)

            vm.onFacts(onVisuals)
            settle()
            assertNull(vm.uiState.value.step)
        }

    @Test
    fun `skipping a chapter ticks it and nothing else starts in this launch`() =
        runTest(main) {
            val data = FakeUserData(startDone)
            val vm = tour(data, onVisuals)
            vm.onSkip()
            vm.onFacts(TourFacts(shown = TourChapter.STUDIO.spots.toSet()))
            settle()
            assertNull(vm.uiState.value.step)
            assertEquals(setOf(TourChapter.START, TourChapter.VISUALS), data.progress.done)
        }

    @Test
    fun `a chapter left half way picks up there next launch`() =
        runTest(main) {
            val data = FakeUserData(startDone)
            val vm = tour(data, onVisuals)
            vm.onPrimary()
            settle()
            assertEquals(TourSpot.STAGE_ARROWS, data.progress.resume)

            val next = tour(FakeUserData(data.progress), onVisuals)
            assertEquals(TourSpot.STAGE_ARROWS, next.uiState.value.step?.spot)
        }

    @Test
    fun `the tour waits while it is held back and comes back on the same step`() =
        runTest(main) {
            val vm = tour(FakeUserData(startDone), onVisuals)
            vm.onPrimary()
            settle()
            vm.onBlocked(true)
            settle()
            assertNull(vm.uiState.value.step)
            vm.onBlocked(false)
            settle()
            assertEquals(TourSpot.STAGE_ARROWS, vm.uiState.value.step?.spot)
        }

    @Test
    fun `a replay opens the screen, waits for it and changes no tick`() =
        runTest(main) {
            val data = FakeUserData(TourProgress.Finished)
            val vm = tour(data, TourFacts())
            vm.replay(listOf(TourChapter.STUDIO, TourChapter.CUSTOMIZE))
            assertEquals(TourNav.CUSTOMIZE, vm.uiState.value.navigate)
            vm.onNavigated()
            vm.onFacts(TourFacts(shown = TourChapter.CUSTOMIZE.spots.toSet(), place = TourPlace.VISUALS))
            settle()
            assertNull(vm.uiState.value.step)
            advanceTimeBy(REPLAY_WAIT)
            assertEquals(TourSpot.CUSTOMIZE_KNOBS, vm.uiState.value.step?.spot)
            assertTrue(vm.uiState.value.replay)

            vm.onSkip()
            settle()
            assertNull(vm.uiState.value.step)
            assertNull(vm.uiState.value.navigate)
            assertFalse(vm.uiState.value.replayNote)
            assertFalse(vm.uiState.value.offerPro)
            assertEquals(TourProgress.Finished, data.progress)
        }

    @Test
    fun `a replay whose screen has no control to point at ends by itself`() =
        runTest(main) {
            val vm = tour(FakeUserData(TourProgress.Finished), TourFacts())
            vm.replay(listOf(TourChapter.STUDIO))
            vm.onNavigated()
            advanceTimeBy(REPLAY_WAIT + SETTLE)
            assertNull(vm.uiState.value.step)
            vm.onFacts(TourFacts(shown = TourChapter.STUDIO.spots.toSet()))
            settle()
            assertNull(vm.uiState.value.step)
        }

    @Test
    fun `a gesture tip is said once, and never over a step`() =
        runTest(main) {
            val data = FakeUserData(TourProgress.Finished)
            val vm = tour(data, onVisuals)
            vm.nudge(GestureTip.SWIPE_STYLES)
            assertEquals(GestureTip.SWIPE_STYLES, vm.uiState.value.tip)
            vm.nudge(GestureTip.HOLD_TO_RECORD)
            assertEquals(GestureTip.SWIPE_STYLES, vm.uiState.value.tip)
            vm.onTipRead()
            settle()
            assertNull(vm.uiState.value.tip)
            assertEquals(setOf(GestureTip.SWIPE_STYLES), data.progress.tips)
            vm.nudge(GestureTip.SWIPE_STYLES)
            assertNull(vm.uiState.value.tip)

            val touring = tour(FakeUserData(startDone), onVisuals)
            touring.nudge(GestureTip.SWIPE_STYLES)
            assertNull(touring.uiState.value.tip)
        }

    @Test
    fun `Reset all clears everything and greets the next launch, not this one`() =
        runTest(main) {
            val data = FakeUserData(TourProgress.Finished.copy(tips = setOf(GestureTip.SWIPE_STYLES)))
            val vm = tour(data, onLibrary)
            vm.reset()
            settle()
            assertEquals(TourProgress(), data.progress)
            assertNull(vm.uiState.value.step)

            val next = tour(FakeUserData(data.progress), onLibrary)
            assertEquals(TourSpot.PLAY_SONG, next.uiState.value.step?.spot)
        }

    private companion object {
        const val SETTLE = TourViewModel.SETTLE_MS + TourViewModel.OFFER_DELAY_MS + 100
        const val REPLAY_WAIT = TourViewModel.REPLAY_SETTLE_MS
    }
}
