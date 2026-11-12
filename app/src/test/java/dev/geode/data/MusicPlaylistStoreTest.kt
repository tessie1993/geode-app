package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.File

/**
 * The legacy file-name migration parses every playlist, so it waits for the store's first use (an
 * IO thread) and is not paid in the constructor, which the session runs on the main thread.
 */
@RunWith(RobolectricTestRunner::class)
class MusicPlaylistStoreTest {
    init {
        // The file stem rule is native (core/tests/look_files_test.cpp); "Road" is its own stem there too.
        PresetStore.fileStem = { it }
    }

    private val context = ApplicationProvider.getApplicationContext<Context>()

    private val dir = File(context.filesDir, "music-playlists")

    private fun legacyFile(): File {
        dir.mkdirs()
        return File(dir, "old-name.json").apply { writeText("""{"name":"Road","tracks":["a"]}""") }
    }

    @Test
    fun constructingTheStoreLeavesTheFolderAlone() {
        val legacy = legacyFile()

        MusicPlaylistStore(context)

        assertTrue(legacy.isFile)
        assertFalse(File(dir, "Road.json").exists())
    }

    @Test
    fun theFirstReadMigratesLegacyFileNames() {
        val legacy = legacyFile()

        val listed = MusicPlaylistStore(context).list()

        assertEquals(listOf(MusicPlaylist("Road", listOf("a"))), listed)
        assertFalse(legacy.exists())
        assertTrue(File(dir, "Road.json").isFile)
    }

    @Test
    fun aWriteBeforeAnyReadMigratesFirst() {
        val legacy = legacyFile()
        val store = MusicPlaylistStore(context)

        store.addTrack("Road", "b")

        assertFalse(legacy.exists())
        assertEquals(listOf(MusicPlaylist("Road", listOf("a", "b"))), store.list())
    }
}
