package dev.geode.data

import android.content.ContextWrapper
import dev.geode.model.LibraryTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * The shell's side of `library.json`: an unreadable file is kept, never written over, and a write happens only when
 * native says the edit changed the library. What the document holds (one bad entry costs only itself, identity,
 * merge, sort) is native's: `core/tests/library_tracks_test.cpp`, so a stand-in answers here.
 */
class TrackLibraryTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val file by lazy { File(tmp.root, "library.json") }

    private val held = listOf(LibraryTrack(uri = "content://a", title = "A"))

    // Text other than "ok" is not a library; an add answers a change, fresh titles answer `changed`.
    private fun stand(
        changed: Boolean,
        written: String,
    ) = object : TrackLibrary.Rules {
        private fun edit(
            text: String,
            changes: Boolean,
        ) = TrackLibrary.Edit(changes, text.isNotBlank() && text != "ok", if (changes) written else "", held)

        override fun read(text: String) = edit(text, false)

        override fun add(
            text: String,
            tracks: List<LibraryTrack>,
        ) = edit(text, true)

        override fun saveInfo(
            text: String,
            track: LibraryTrack,
        ) = edit(text, true)

        override fun applyFresh(
            text: String,
            fresh: List<TrackLibrary.Fresh>,
        ) = edit(text, changed)
    }

    private fun library(rules: TrackLibrary.Rules): TrackLibrary {
        TrackLibrary.rules = rules
        return TrackLibrary(
            object : ContextWrapper(null) {
                override fun getFilesDir(): File = tmp.root
            },
        )
    }

    @Test
    fun aSecondUnreadableLibraryKeepsTheFirstOnesCopy() {
        val library = library(stand(changed = false, written = ""))
        file.writeText("first")
        library.list()
        file.writeText("second")
        library.list()
        assertEquals("first", File(tmp.root, "library.json.corrupt").readText())
        assertEquals("second", File(tmp.root, "library.json.2.corrupt").readText())
        assertFalse(file.exists())
    }

    @Test
    fun anAddToAnUnreadableLibrarySetsItAsideAndWritesTheNewOne() {
        val library = library(stand(changed = true, written = "{new}"))
        file.writeText("first")
        assertEquals(held, library.addAll(held))
        assertEquals("first", File(tmp.root, "library.json.corrupt").readText())
        assertEquals("{new}", file.readText())
    }

    @Test
    fun anEditNativeSaysChangedNothingWritesNothing() {
        val library = library(stand(changed = false, written = "{new}"))
        assertEquals(held, library.applyFreshTitles(listOf(TrackLibrary.Fresh("content://a", "A", ""))))
        assertFalse(file.exists())
    }

    @Test
    fun anEditNativeSaysChangedIsWritten() {
        val library = library(stand(changed = true, written = "{new}"))
        file.writeText("ok")
        library.applyFreshTitles(listOf(TrackLibrary.Fresh("content://a", "B", "")))
        assertEquals("{new}", file.readText())
    }
}
