package dev.geode.data

import android.content.ContextWrapper
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/** `library.json`: one bad entry costs only itself, and an unreadable file is kept, never written over. */
class TrackLibraryTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val file by lazy { File(tmp.root, "library.json") }

    private fun library() =
        TrackLibrary(
            object : ContextWrapper(null) {
                override fun getFilesDir(): File = tmp.root
            },
        )

    @Test
    fun anEntryWithNoUriDropsOnlyItself() {
        val json = """{"version":3,"tracks":[{"uri":"content://a","title":"A"},{"title":"no uri"},7,{"uri":"content://b","title":"B"}]}"""
        assertEquals(listOf("content://a", "content://b"), TrackLibrary.parseOrNull(json)?.map { it.uri })
    }

    @Test
    fun anUnreadableContainerIsNull() {
        assertNull(TrackLibrary.parseOrNull("{\"tracks\":"))
        assertNull(TrackLibrary.parseOrNull("{\"version\":3}"))
    }

    @Test
    fun aSecondUnreadableLibraryKeepsTheFirstOnesCopy() {
        file.writeText("first")
        assertEquals(emptyList<Any>(), library().list())
        file.writeText("second")
        assertEquals(emptyList<Any>(), library().list())
        assertEquals("first", File(tmp.root, "library.json.corrupt").readText())
        assertEquals("second", File(tmp.root, "library.json.2.corrupt").readText())
        assertFalse(file.exists())
    }
}
