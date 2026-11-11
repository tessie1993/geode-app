package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class LibraryPrefsStoreTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("library-prefs-test", Context.MODE_PRIVATE)

    @Test
    fun theMinimumDefaultsToThirtySeconds() {
        assertEquals(30, LibraryPrefsStore(prefs).minTrackSeconds())
    }

    @Test
    fun theMinimumRoundTripsUnderItsStableKey() {
        LibraryPrefsStore(prefs).setMinTrackSeconds(60)
        assertEquals(60, LibraryPrefsStore(prefs).minTrackSeconds())
        assertEquals("min_track_seconds", LibraryPrefsStore.KEY_MIN_TRACK_SECONDS)
        assertEquals(60, prefs.getInt("min_track_seconds", -1))
    }

    @Test
    fun offIsZeroAndNegativeValuesClampToIt() {
        val store = LibraryPrefsStore(prefs)
        store.setMinTrackSeconds(0)
        assertEquals(0, store.minTrackSeconds())
        store.setMinTrackSeconds(-5)
        assertEquals(0, prefs.getInt("min_track_seconds", -1))
    }
}
