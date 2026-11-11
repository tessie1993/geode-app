package dev.geode.ui

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import dev.geode.data.MusicLibraryRepository
import dev.geode.data.MusicPlaylist
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Playlist edits leave the caller's thread (a tap is on main, and each edit reads the folder and
 * fsyncs a file) and still land in the order they were asked for.
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class MusicLibraryPlaylistEditsTest {
    private val application = ApplicationProvider.getApplicationContext<Application>()

    private val prefs = application.getSharedPreferences("playlist-edits-test", Context.MODE_PRIVATE)

    @Test
    fun anEditDoesNothingOnTheCallersThread() =
        runTest {
            val controller = MusicLibraryRepository(application, prefs, backgroundScope, StandardTestDispatcher(testScheduler))

            controller.createMusicPlaylist("Road")
            assertEquals(emptyList<MusicPlaylist>(), controller.library.value.playlists)

            runCurrent()
            assertEquals(listOf(MusicPlaylist("Road")), controller.library.value.playlists)
        }

    @Test
    fun aQueueIsSavedAsOnePlaylistWithEachTrackOnce() =
        runTest {
            val controller = MusicLibraryRepository(application, prefs, backgroundScope, StandardTestDispatcher(testScheduler))

            controller.createMusicPlaylist("Queue", listOf("a", "b", "a", "c"))
            runCurrent()

            assertEquals(listOf(MusicPlaylist("Queue", listOf("a", "b", "c"))), controller.library.value.playlists)
        }

    @Test
    fun editsLandInTheOrderTheyWereAskedFor() =
        runTest {
            val controller = MusicLibraryRepository(application, prefs, backgroundScope, StandardTestDispatcher(testScheduler))

            controller.createMusicPlaylist("Mix")
            controller.addTrackToPlaylist("Mix", "a")
            controller.addTrackToPlaylist("Mix", "b")
            controller.moveMusicPlaylistTrack("Mix", 1, 0)
            controller.removeTrackFromPlaylist("Mix", "a")
            controller.renameMusicPlaylist("Mix", "Mix 2")
            runCurrent()

            assertEquals(listOf(MusicPlaylist("Mix 2", listOf("b"))), controller.library.value.playlists)
        }
}
