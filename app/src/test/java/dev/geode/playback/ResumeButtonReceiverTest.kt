package dev.geode.playback

import dev.geode.data.SessionStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** A play key starts the playback service only when something will play and take it to the foreground. */
class ResumeButtonReceiverTest {
    private val track = SessionStore.SavedTrack("content://media/1", "Track", "Artist")
    private val queue = ResumeSource.Queue(SessionStore.Saved(listOf(track), 0, 0L))

    @Test
    fun nothingLiveAndNothingSavedDeclinesTheStart() {
        assertFalse(playKeyHasSomethingToPlay(liveQueue = false) { ResumeSource.Nothing })
    }

    @Test
    fun aSavedQueueStartsTheService() {
        assertTrue(playKeyHasSomethingToPlay(liveQueue = false) { queue })
    }

    @Test
    fun aLiveQueueStartsTheServiceWithoutReadingTheSavedOne() {
        var reads = 0
        assertTrue(
            playKeyHasSomethingToPlay(liveQueue = true) {
                reads++
                ResumeSource.Nothing
            },
        )
        assertEquals(0, reads)
    }
}
