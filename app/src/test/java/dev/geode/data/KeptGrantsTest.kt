package dev.geode.data

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** A grant goes back once nothing uses it, and never while another of the app's places still names the same URI. */
@RunWith(RobolectricTestRunner::class)
class KeptGrantsTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val resolver get() = context.contentResolver
    private val both = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

    private fun held(uri: String): Boolean = resolver.persistedUriPermissions.any { it.uri.toString() == uri }

    private fun folder(name: String): String = "content://com.example.docs/tree/$name"

    private fun prefs(name: String) = context.getSharedPreferences(name, Context.MODE_PRIVATE)

    @Test
    fun replacingRecordsFolderGivesTheOldGrantBack() {
        val store = RecordSaveStore(context)
        assertTrue(store.choose(SaveTarget.Folder(folder("a"))))
        assertTrue(store.choose(SaveTarget.Folder(folder("b"))))
        assertFalse(held(folder("a")))
        assertTrue(held(folder("b")))
    }

    @Test
    fun aFolderThatIsAlsoThePresetFolderKeepsItsGrant() {
        prefs(GeodePrefsFiles.GENERAL).edit().putString(LibrarySettingsCodec.KEY_PRESET_MIRROR_URI, folder("c")).commit()
        val store = RecordSaveStore(context)
        store.choose(SaveTarget.Folder(folder("c")))
        store.choose(SaveTarget.VideosLibrary)
        assertTrue(held(folder("c")))
    }

    @Test
    fun keptFoldersNamesThePresetRecordAndMusicFolders() {
        prefs(GeodePrefsFiles.LIBRARY).edit().putStringSet(KEY_ROOTS, setOf(folder("music"))).commit()
        RecordSaveStore(context).choose(SaveTarget.Folder(folder("rec")))
        assertEquals(setOf(folder("music"), folder("rec"), folder("new")), keptFolders(context, preset = folder("new")))
    }

    @Test
    fun aReplacedPresetFolderGoesBackUnlessAnotherPlaceUsesIt() =
        runTest {
            val source = MediaStoreSource(context, Dispatchers.Unconfined)
            resolver.takePersistableUriPermission(Uri.parse(folder("old")), both)
            source.releaseFolderAccess(folder("old"), preset = folder("new"))
            assertFalse(held(folder("old")))

            prefs(GeodePrefsFiles.LIBRARY).edit().putStringSet(KEY_ROOTS, setOf(folder("shared"))).commit()
            resolver.takePersistableUriPermission(Uri.parse(folder("shared")), both)
            source.releaseFolderAccess(folder("shared"), preset = null)
            assertTrue(held(folder("shared")))
        }

    @Test
    fun aLoopClipTheLibraryAlsoHoldsKeepsItsGrant() =
        runTest {
            val clip = Uri.parse("content://com.example.docs/document/clip")
            val song = Uri.parse("content://com.example.docs/document/song")
            listOf(clip, song).forEach { resolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            MediaStoreSource(context, Dispatchers.Unconfined).releaseReadAccess(listOf(clip, song), held = setOf(song.toString()))
            assertFalse(held(clip.toString()))
            assertTrue(held(song.toString()))
        }
}
