package dev.geode.model

import dev.geode.model.TutorialChapter.SETTINGS
import dev.geode.model.TutorialChapter.WELCOME
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The tour is one flat pager over every card of every chapter, so the interesting part is the
 * arithmetic at chapter edges: where a page lands, what Skip chapter jumps to, which moves count
 * a chapter as done, and that the saved form survives anything stored on disk.
 */
class TutorialTourTest {
    private val tour = TutorialTour(listOf(WELCOME, SETTINGS))

    @Test
    fun `pages run through every card of every chapter in order`() {
        assertEquals(WELCOME.cards + SETTINGS.cards, tour.pageCount)
        assertEquals(TutorialPosition(WELCOME, 0), tour.positionOf(0))
        assertEquals(TutorialPosition(SETTINGS, 0), tour.positionOf(WELCOME.cards))
        assertEquals(TutorialPosition(SETTINGS, SETTINGS.cards - 1), tour.positionOf(tour.pageCount - 1))
    }

    @Test
    fun `pageOf inverts positionOf, and an unknown chapter starts the tour`() {
        repeat(tour.pageCount) { assertEquals(it, tour.pageOf(tour.positionOf(it))) }
        assertEquals(0, TutorialTour(listOf(WELCOME)).pageOf(TutorialPosition(SETTINGS, 1)))
        // A card index from an older build with more cards is clamped, not trusted.
        assertEquals(tour.pageCount - 1, tour.pageOf(TutorialPosition(SETTINGS, 99)))
    }

    @Test
    fun `skip chapter jumps to the next chapter's first page and ends after the last`() {
        assertEquals(WELCOME.cards, tour.nextChapterPage(0))
        assertNull(tour.nextChapterPage(tour.pageCount - 1))
    }

    @Test
    fun `moving forward one page off a chapter's last card completes it`() {
        val lastWelcome = WELCOME.cards - 1
        assertNull(tour.completedBy(0, 1))
        assertEquals(WELCOME, tour.completedBy(lastWelcome, lastWelcome + 1))
        // Finishing is a move to one past the last page.
        assertEquals(SETTINGS, tour.completedBy(tour.pageCount - 1, tour.pageCount))
    }

    @Test
    fun `moving back or skipping ahead completes nothing`() {
        val lastWelcome = WELCOME.cards - 1
        assertNull(tour.completedBy(lastWelcome + 1, lastWelcome))
        assertNull(tour.completedBy(0, tour.pageCount - 1))
        assertNull(tour.completedBy(tour.pageCount, tour.pageCount + 1))
    }

    @Test
    fun `progress round-trips through its stored form`() {
        val progress = TutorialProgress(done = setOf(WELCOME, SETTINGS), resume = TutorialPosition(SETTINGS, 1))
        assertEquals(progress, TutorialProgress.decode(progress.encodeDone(), progress.encodeResume()))
        assertEquals(TutorialProgress(), TutorialProgress.decode(null, null))
    }

    @Test
    fun `stored progress from another build is read leniently`() {
        val progress = TutorialProgress.decode("WELCOME,GONE,,SETTINGS", "GONE:1")
        assertEquals(setOf(WELCOME, SETTINGS), progress.done)
        assertNull(progress.resume)
        assertNull(TutorialProgress.decode(null, "SETTINGS:x").resume)
        assertTrue(TutorialProgress.decode("", "").done.isEmpty())
    }
}
