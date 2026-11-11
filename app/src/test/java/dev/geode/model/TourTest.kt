package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** The tour's rules: which step shows, what a missing control does, what is remembered. */
class TourTest {
    private val visualsShown = TourFacts(shown = TourChapter.VISUALS.spots.toSet(), place = TourPlace.VISUALS)

    @Test
    fun `every chapter has steps and START leads with the Library tab`() {
        TourChapter.entries.forEach { assertTrue(it.name, it.spots.isNotEmpty()) }
        assertEquals(TourSpot.NAV_LIBRARY, TourChapter.START.spots.first())
    }

    @Test
    fun `a pointer whose control is missing is passed over, not waited for`() {
        val facts = visualsShown.copy(shown = setOf(TourSpot.VISUALS_TABS, TourSpot.STAGE_RECORD))
        val run = TourRun(listOf(TourChapter.VISUALS))
        val first = Tour.step(run, facts)!!
        assertEquals(TourSpot.VISUALS_TABS, first.spot)
        assertEquals(1, first.index)
        assertEquals(2, first.count)
        val second = Tour.step(Tour.pass(run, first.spot), facts)!!
        assertEquals(TourSpot.STAGE_RECORD, second.spot)
        assertTrue(second.last)
    }

    @Test
    fun `a chapter with no control on screen has no step`() {
        assertNull(Tour.step(TourRun(listOf(TourChapter.STUDIO)), visualsShown))
        assertNull(Tour.step(TourRun(emptyList()), visualsShown))
    }

    @Test
    fun `the first run skips Open Library on an install that opens on Library`() {
        val onLibrary = TourFacts(shown = setOf(TourSpot.NAV_LIBRARY, TourSpot.NAV_VISUALS), place = TourPlace.LIBRARY)
        val step = Tour.step(TourRun(listOf(TourChapter.START)), onLibrary)!!
        assertEquals(TourSpot.PLAY_SONG, step.spot)
        assertEquals(TourStepKind.WAIT, step.spot.kind)
    }

    @Test
    fun `the first run asks for Library from another screen`() {
        val onPlayer = TourFacts(shown = setOf(TourSpot.NAV_LIBRARY, TourSpot.NAV_VISUALS))
        val step = Tour.step(TourRun(listOf(TourChapter.START)), onPlayer)!!
        assertEquals(TourSpot.NAV_LIBRARY, step.spot)
        assertEquals(1, step.index)
        assertEquals(2, step.count)
    }

    @Test
    fun `playing a song ends the wait and Visuals ends the first run`() {
        val run = TourRun(listOf(TourChapter.START))
        val playing = TourFacts(shown = setOf(TourSpot.NAV_LIBRARY, TourSpot.NAV_VISUALS), place = TourPlace.LIBRARY, playing = true)
        assertTrue(Tour.reached(TourSpot.PLAY_SONG, playing))
        assertEquals(TourSpot.NAV_VISUALS, Tour.step(run, playing)!!.spot)
        assertNull(Tour.step(run, playing.copy(place = TourPlace.VISUALS)))
    }

    @Test
    fun `a pointer is never reached by itself`() {
        TourSpot.entries.filter { it.kind == TourStepKind.POINT }.forEach {
            assertFalse(it.name, Tour.reached(it, visualsShown.copy(playing = true)))
        }
    }

    @Test
    fun `the step count stays put as steps are passed`() {
        var run = TourRun(listOf(TourChapter.VISUALS))
        val counts =
            TourChapter.VISUALS.spots.map {
                val step = Tour.step(run, visualsShown)!!
                run = Tour.pass(run, step.spot)
                step.index to step.count
            }
        assertEquals(listOf(1 to 4, 2 to 4, 3 to 4, 4 to 4), counts)
        assertNull(Tour.step(run, visualsShown))
    }

    @Test
    fun `START is due first, then a chapter whose controls are on screen`() {
        assertEquals(TourChapter.START, Tour.due(TourProgress(), visualsShown))
        val started = TourProgress(done = setOf(TourChapter.START))
        assertEquals(TourChapter.VISUALS, Tour.due(started, visualsShown))
        assertNull(Tour.due(started, TourFacts()))
        assertNull(Tour.due(started.copy(done = started.done + TourChapter.VISUALS), visualsShown))
        assertNull(Tour.due(TourProgress.Finished, visualsShown))
    }

    @Test
    fun `a chapter picks up at the step it was left on`() {
        val left = TourProgress(done = setOf(TourChapter.START), resume = TourSpot.STAGE_TOOLS)
        assertEquals(2, Tour.resume(TourChapter.VISUALS, left).at)
        assertEquals(0, Tour.resume(TourChapter.STUDIO, left).at)
    }

    @Test
    fun `finishing ticks the chapter and skipping the first run turns the tour off`() {
        val run = TourRun(listOf(TourChapter.VISUALS))
        val before = TourProgress(done = setOf(TourChapter.START), resume = TourSpot.STAGE_TOOLS)
        assertEquals(TourProgress(done = setOf(TourChapter.START, TourChapter.VISUALS)), Tour.ended(before, run, skipped = false))
        assertEquals(TourProgress(done = setOf(TourChapter.START, TourChapter.VISUALS)), Tour.ended(before, run, skipped = true))
        assertEquals(TourProgress.Finished, Tour.ended(TourProgress(), TourRun(listOf(TourChapter.START)), skipped = true))
        assertEquals(setOf(TourChapter.START), Tour.ended(TourProgress(), TourRun(listOf(TourChapter.START)), skipped = false).done)
    }

    @Test
    fun `a replay plays in tour order and changes no progress`() {
        val run = Tour.replay(listOf(TourChapter.STUDIO, TourChapter.VISUALS))!!
        assertEquals(listOf(TourChapter.VISUALS, TourChapter.STUDIO), run.chapters)
        assertTrue(run.replay)
        assertEquals(TourProgress(), Tour.ended(TourProgress(), run, skipped = true))
        assertEquals(listOf(TourChapter.STUDIO), Tour.nextChapter(run)!!.chapters)
        assertNull(Tour.nextChapter(Tour.nextChapter(run)!!))
        assertNull(Tour.replay(emptyList()))
    }

    @Test
    fun `the Pro offer follows the first look at Visuals or a skipped tour, never a replay`() {
        assertTrue(Tour.offersPro(TourRun(listOf(TourChapter.VISUALS)), skipped = false))
        assertTrue(Tour.offersPro(TourRun(listOf(TourChapter.START)), skipped = true))
        assertFalse(Tour.offersPro(TourRun(listOf(TourChapter.START)), skipped = false))
        assertFalse(Tour.offersPro(TourRun(listOf(TourChapter.CUSTOMIZE)), skipped = false))
        assertFalse(Tour.offersPro(TourRun(listOf(TourChapter.VISUALS), replay = true), skipped = false))
    }

    @Test
    fun `progress survives a round trip and ignores names it does not know`() {
        val progress =
            TourProgress(
                done = setOf(TourChapter.START, TourChapter.STUDIO),
                resume = TourSpot.CUSTOMIZE_LIST,
                tips = setOf(GestureTip.SWIPE_STYLES),
            )
        assertEquals(progress, TourProgress.decode(progress.encodeDone(), progress.encodeResume(), progress.encodeTips()))
        assertEquals(TourProgress(), TourProgress.decode(null, null, null))
        assertEquals(
            TourProgress(done = setOf(TourChapter.VISUALS)),
            TourProgress.decode("PLAYER,VISUALS,ANYWHERE,", "PLAYER:2", "GONE,,SWIPE"),
        )
    }

    @Test
    fun `the old tour's saved progress maps onto the new chapters`() {
        // Mid-way through the old tour: its guided path (WELCOME) is First steps, and the old
        // "CHAPTER:card" resume names no step, so the next chapter starts from its top.
        val old = TourProgress.decode("WELCOME,PLAYER,LIBRARY,CUSTOMIZE,LIVE_VIEW,PRO", "CUSTOMIZE:3", null)
        assertEquals(setOf(TourChapter.START, TourChapter.CUSTOMIZE), old.done)
        assertNull(old.resume)
        assertEquals(TourChapter.VISUALS, Tour.due(old, visualsShown))
        assertEquals(0, Tour.resume(TourChapter.VISUALS, old).at)
        // Reset in an old build: nothing done, so First steps leads again.
        assertEquals(TourChapter.START, Tour.due(TourProgress.decode("", "", ""), visualsShown))
    }

    @Test
    fun `migrated progress saves under the new names only`() {
        val old = TourProgress.decode("WELCOME,SETTINGS,STUDIO", null, "SWIPE_STYLES,HOLD_TO_RECORD")
        assertEquals(setOf("START", "STUDIO"), old.encodeDone().split(',').toSet())
        assertNull(old.encodeResume())
        assertEquals(old, TourProgress.decode(old.encodeDone(), old.encodeResume(), old.encodeTips()))
    }

    @Test
    fun `an install that ended the old tour sees no chapter by itself`() {
        val old = TourProgress.decode("WELCOME", null, null, legacySeen = true)
        assertEquals(TourChapter.entries.toSet(), old.done)
        assertNull(Tour.due(old, visualsShown))
    }

    private val safe = TourRect(16, 60, 344, 700)

    @Test
    fun `the card goes on the side of the control with room, inside the safe area`() {
        // A tab at the bottom: above it.
        assertEquals(130 to 484, Tour.placeCard(TourRect(200, 640, 260, 700), 200, 140, safe, gap = 16))
        // Icons at the top right: below, pulled in from the edge.
        assertEquals(144 to 124, Tour.placeCard(TourRect(240, 60, 344, 108), 200, 140, safe, gap = 16))
        // A panel as tall as the window, on the right: beside it.
        assertEquals(24 to 310, Tour.placeCard(TourRect(240, 60, 344, 700), 200, 140, safe, gap = 16))
    }

    @Test
    fun `a control that leaves no room has the card over its bottom, and no control centres it`() {
        assertEquals(80 to 560, Tour.placeCard(TourRect(16, 60, 344, 700), 200, 140, safe, gap = 16))
        assertEquals(80 to 310, Tour.placeCard(null, 200, 140, safe, gap = 16))
    }

    @Test
    fun `a card larger than the safe area starts at its corner`() {
        assertEquals(16 to 60, Tour.placeCard(null, 400, 900, safe, gap = 16))
    }
}
