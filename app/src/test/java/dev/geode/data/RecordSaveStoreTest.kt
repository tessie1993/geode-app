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

@RunWith(RobolectricTestRunner::class)
class RecordSaveStoreTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val raw get() = context.getSharedPreferences("geode-record", Context.MODE_PRIVATE)

    @Test
    fun notAskedYetReadsAsNull() {
        assertNull(RecordSaveStore(context).target())
    }

    @Test
    fun theVideosLibraryAnswerIsRememberedUnderTheStableFileAndKey() {
        assertTrue(RecordSaveStore(context).choose(SaveTarget.VideosLibrary))
        assertEquals("library", raw.getString("save_target", null))
        assertEquals(SaveTarget.VideosLibrary, RecordSaveStore(context).target())
    }

    @Test
    fun aOneOffAnswerIsNotKept() {
        assertTrue(RecordSaveStore(context).choose(SaveTarget.VideosLibrary, remember = false))
        assertNull(RecordSaveStore(context).target())
    }

    @Test
    fun clearForgetsTheAnswer() {
        val store = RecordSaveStore(context)
        store.choose(SaveTarget.VideosLibrary)
        store.clear()
        assertFalse(raw.contains("save_target"))
        assertNull(store.target())
    }
}
