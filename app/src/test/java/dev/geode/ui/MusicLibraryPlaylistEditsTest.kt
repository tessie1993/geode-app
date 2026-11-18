package dev.geode.ui

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import dev.geode.data.MusicLibraryRepository
import dev.geode.data.MusicPlaylist
import dev.geode.data.MusicPlaylistStore
import dev.geode.data.MusicPlaylistStore.Edit
import dev.geode.data.PresetStore
import dev.geode.engine.bridge.LibraryNative
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
 * fsyncs a file) and still land in the order they were asked for. What each edit does to a playlist
 * (each track once, a free name, the clamps) is native's (core/tests/library_playlist_test.cpp); a
 * stand-in records the edits here.
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class MusicLibraryPlaylistEditsTest {
    private val application = ApplicationProvider.getApplicationContext<Application>()

    private val prefs = application.getSharedPreferences("playlist-edits-test", Context.MODE_PRIVATE)

    private val edits = mutableListOf<Edit>()

    init {
        PresetStore.fileStem = { it }
        MusicPlaylistStore.rules =
            object : MusicPlaylistStore.Rules {
                override fun list(texts: List<String>): List<MusicPlaylist> = emptyList()

                override fun apply(
                    edit: Edit,
                    texts: List<String>,
                    named: String?,
                ): MusicPlaylistStore.Outcome {
                    edits += edit
                    return MusicPlaylistStore.Outcome(write = false, ok = true, name = edit.name, deleteName = "", json = "")
                }

                override fun migration(
                    fileNames: List<String>,
                    texts: List<String>,
                ): List<Pair<String, String>> = emptyList()

                override fun importBase(
                    parsedName: String,
                    fileName: String,
                    untitled: String,
                ): String = parsedName
            }
    }

    @Test
    fun anEditDoesNothingOnTheCallersThread() =
        runTest {
            val controller = MusicLibraryRepository(application, prefs, backgroundScope, StandardTestDispatcher(testScheduler))

            controller.createMusicPlaylist("Road")
            assertEquals(emptyList<Edit>(), edits)

            runCurrent()
            assertEquals(listOf(Edit(LibraryNative.MUSIC_PLAYLIST_CREATE, "Road")), edits)
        }

    @Test
    fun aQueueIsOneEditHoldingEveryTrack() =
        runTest {
            val controller = MusicLibraryRepository(application, prefs, backgroundScope, StandardTestDispatcher(testScheduler))

            controller.createMusicPlaylist("Queue", listOf("a", "b", "a", "c"))
            runCurrent()

            assertEquals(listOf(Edit(LibraryNative.MUSIC_PLAYLIST_CREATE, "Queue", tracks = listOf("a", "b", "a", "c"))), edits)
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

            assertEquals(
                listOf(
                    Edit(LibraryNative.MUSIC_PLAYLIST_CREATE, "Mix"),
                    Edit(LibraryNative.MUSIC_PLAYLIST_ADD, "Mix", track = "a"),
                    Edit(LibraryNative.MUSIC_PLAYLIST_ADD, "Mix", track = "b"),
                    Edit(LibraryNative.MUSIC_PLAYLIST_MOVE, "Mix", from = 1, to = 0),
                    Edit(LibraryNative.MUSIC_PLAYLIST_REMOVE, "Mix", track = "a"),
                    Edit(LibraryNative.MUSIC_PLAYLIST_RENAME, "Mix", newName = "Mix 2"),
                ),
                edits,
            )
        }
}
