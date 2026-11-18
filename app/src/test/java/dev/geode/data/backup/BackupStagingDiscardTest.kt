package dev.geode.data.backup

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.File

/**
 * What `GeodeContainer` runs at start: staging a previous process left is dropped, and the user's
 * originals a swap had set aside go back first wherever nothing took their place.
 */
@RunWith(RobolectricTestRunner::class)
class BackupStagingDiscardTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val work get() = File(context.noBackupFilesDir, "backup-restore")

    private fun write(
        file: File,
        text: String,
    ) = file.apply { parentFile?.mkdirs() }.writeText(text)

    @Test
    fun staleStagingGoesAndSetAsideOriginalsComeBack() =
        runTest {
            write(File(work, "staged/presets/new.json"), "staged")
            write(File(work, "import.zip"), "zip")
            write(File(work, "aside/presets/mine.json"), "original")
            write(File(work, "aside/playlists/placed.json"), "old")
            write(File(context.filesDir, "playlists/placed.json"), "new")

            BackupRepository(context, Dispatchers.Unconfined).discard()

            assertFalse(work.exists())
            assertEquals("original", File(context.filesDir, "presets/mine.json").readText())
            assertEquals("new", File(context.filesDir, "playlists/placed.json").readText())
        }
}
