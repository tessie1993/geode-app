package dev.geode.playback

import dev.geode.data.SessionStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

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

    /** The stock Media3 receiver in its place would start the service for every play key again. */
    @Test
    fun theManifestSendsMediaButtonsToThisReceiver() {
        val android = "http://schemas.android.com/apk/res/android"
        val manifest =
            DocumentBuilderFactory
                .newInstance()
                .apply { isNamespaceAware = true }
                .newDocumentBuilder()
                .parse(File("src/main/AndroidManifest.xml"))
                .documentElement
        val receivers = manifest.getElementsByTagName("receiver").let { list -> (0 until list.length).map { list.item(it) as Element } }
        val forMediaButtons =
            receivers.filter { receiver ->
                val actions = receiver.getElementsByTagName("action")
                (0 until actions.length).any {
                    (actions.item(it) as Element).getAttributeNS(android, "name") == "android.intent.action.MEDIA_BUTTON"
                }
            }
        assertEquals(listOf(".playback.ResumeButtonReceiver"), forMediaButtons.map { it.getAttributeNS(android, "name") })
    }
}
