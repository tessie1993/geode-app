package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import dev.geode.editor.TrackRef
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.File

/** A first recording waiting for the save sheet survives a restart, out of reach of the cache's scratch sweep. */
@RunWith(RobolectricTestRunner::class)
class RecordFilesWaitingTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    private fun scratch(ms: Long): File = File(context.cacheDir, "studio-record-$ms.mp4").apply { writeText("mp4") }

    @Test
    fun aParkedRecordingLeavesTheCacheAndIsFoundByTheNextProcess() {
        val file = scratch(1L)
        val track = TrackRef("content://song/1", "Song", 1234L)
        val parked = RecordFiles(context).park(file, "Song", 5_000L, track)

        assertFalse(file.exists())
        assertFalse(parked.file.name.startsWith("studio-"))
        assertTrue(parked.file.path.startsWith(context.noBackupFilesDir.path))

        val found = RecordFiles(context).waiting()
        assertNotNull(found)
        assertEquals(parked.file, found!!.file)
        assertEquals("Song", found.title)
        assertEquals(5_000L, found.durationMs)
        assertEquals(track, found.track)
    }

    @Test
    fun withoutItsNoteARecordingStillWaitsUnderTheDefaultName() {
        val parked = RecordFiles(context).park(scratch(2L), "Mic", 10L, null)
        File(parked.file.path.removeSuffix(".mp4") + ".json").delete()

        val found = RecordFiles(context).waiting()
        assertEquals(parked.file, found?.file)
        assertEquals("Recording", found?.title)
        assertNull(found?.track)
    }

    @Test
    fun nothingWaitsOnAFreshInstall() {
        assertNull(RecordFiles(context).waiting())
    }
}
