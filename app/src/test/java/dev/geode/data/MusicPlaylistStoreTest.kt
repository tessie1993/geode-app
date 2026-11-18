package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.File

/**
 * The store's side of the playlists: the legacy file-name migration waits for the store's first use (an IO
 * thread) and is not paid in the constructor, which the session runs on the main thread; an edit hands native the
 * folder and does what it answers. The rules themselves are native's (core/tests/library_playlist_test.cpp), so a
 * stand-in answers here.
 */
@RunWith(RobolectricTestRunner::class)
class MusicPlaylistStoreTest {
    private val asked = mutableListOf<String>()

    private var named: String? = "not asked"

    private var answer = MusicPlaylistStore.Outcome(write = true, ok = true, name = "Road", deleteName = "", json = "{\"native\":1}")

    init {
        // The file stem rule is native (core/tests/look_files_test.cpp); "Road" is its own stem there too.
        PresetStore.fileStem = { it }
        MusicPlaylistStore.rules =
            object : MusicPlaylistStore.Rules {
                override fun list(texts: List<String>): List<MusicPlaylist> {
                    asked += "list"
                    return emptyList()
                }

                override fun apply(
                    edit: MusicPlaylistStore.Edit,
                    texts: List<String>,
                    named: String?,
                ): MusicPlaylistStore.Outcome {
                    asked += "apply"
                    this@MusicPlaylistStoreTest.named = named
                    return answer
                }

                // Native's plan for the legacy file: it holds "Road", so it moves to Road.json.
                override fun migration(
                    fileNames: List<String>,
                    texts: List<String>,
                ): List<Pair<String, String>> {
                    asked += "migration"
                    return if ("old-name.json" in fileNames) listOf("old-name.json" to "Road.json") else emptyList()
                }

                override fun importBase(
                    parsedName: String,
                    fileName: String,
                    untitled: String,
                ): String = parsedName
            }
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
        assertEquals(emptyList<String>(), asked)
    }

    @Test
    fun theFirstReadMigratesLegacyFileNames() {
        val legacy = legacyFile()

        MusicPlaylistStore(context).list()

        assertEquals(listOf("migration", "list"), asked)
        assertFalse(legacy.exists())
        assertTrue(File(dir, "Road.json").isFile)
    }

    @Test
    fun aWriteBeforeAnyReadMigratesFirst() {
        val legacy = legacyFile()
        val store = MusicPlaylistStore(context)

        store.addTrack("Road", "b")

        assertEquals(listOf("migration", "apply"), asked)
        assertFalse(legacy.exists())
        assertEquals("{\"native\":1}", File(dir, "Road.json").readText())
    }

    @Test
    fun anEditHandsNativeTheNamedFileOrNothing() {
        val store = MusicPlaylistStore(context)

        store.removeTrack("Gone", "b")
        assertNull(named)

        File(dir, "Road.json").writeText("garbage")
        store.addTrack("Road", "c")
        assertEquals("garbage", named)
    }

    @Test
    fun aRenameWritesTheNewFileBeforeDeletingTheOld() {
        dir.mkdirs()
        val old = File(dir, "Old.json").apply { writeText("old") }
        answer = MusicPlaylistStore.Outcome(write = true, ok = true, name = "New", deleteName = "Old", json = "new")

        assertTrue(MusicPlaylistStore(context).rename("Old", "New"))

        assertEquals("new", File(dir, "New.json").readText())
        assertFalse(old.exists())
    }

    @Test
    fun anEditNativeRefusesWritesNothing() {
        answer = MusicPlaylistStore.Outcome(write = false, ok = false, name = "New", deleteName = "", json = "")

        assertFalse(MusicPlaylistStore(context).rename("Old", "New"))

        assertFalse(File(dir, "New.json").exists())
    }
}
