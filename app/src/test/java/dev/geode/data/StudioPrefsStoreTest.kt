package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class StudioPrefsStoreTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("studio-prefs-test", Context.MODE_PRIVATE)

    @Test
    fun aFreshInstallHasNoTabAndNoAnswer() {
        val store = StudioPrefsStore(prefs)
        assertNull(store.lastTab())
        assertNull(store.keepAsProject())
    }

    @Test
    fun theLastTabRoundTripsAndPersistsUnderItsKey() {
        StudioPrefsStore(prefs).saveLastTab("MY_VIDEOS")
        assertEquals("MY_VIDEOS", StudioPrefsStore(prefs).lastTab())
        assertEquals("MY_VIDEOS", prefs.getString("studio_last_tab", null))
    }

    @Test
    fun bothKeepAnswersAreKeptDistinctFromNotAskedYet() {
        val store = StudioPrefsStore(prefs)
        store.saveKeepAsProject(false)
        assertEquals(false, store.keepAsProject())
        store.saveKeepAsProject(true)
        assertEquals(true, store.keepAsProject())
        assertEquals(true, prefs.getBoolean("studio_keep_as_project", false))
    }
}
