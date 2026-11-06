package dev.geode.model

import dev.geode.model.TutorialChapter.SETTINGS
import dev.geode.model.TutorialChapter.WELCOME
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** What the tour remembers between launches survives anything stored on disk. */
class TutorialProgressTest {
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
