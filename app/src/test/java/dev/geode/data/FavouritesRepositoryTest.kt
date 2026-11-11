package dev.geode.data

import android.content.Context
import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

/** Every heart (Player, Library, the notification) writes one file; each must see the others' writes. */
@RunWith(RobolectricTestRunner::class)
class FavouritesRepositoryTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("favourites-test", Context.MODE_PRIVATE)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO.limitedParallelism(1))

    @After
    fun tearDown() = scope.cancel()

    @Test
    fun aToggleIsEmittedAndSavedUnderTheTracksUri() =
        runBlocking {
            val repository = SharedPrefsFavouritesRepository(prefs, scope)
            assertTrue(repository.toggle(TRACK))
            assertEquals(setOf(TRACK), repository.favourites.value)
            assertTrue(FavouritesStore(prefs).isFavourite(TRACK))
            assertFalse(repository.toggle(TRACK))
            assertEquals(emptySet<String>(), repository.favourites.value)
        }

    @Test
    fun theNotificationsWriteReachesTheAppsState() =
        runBlocking {
            val repository = SharedPrefsFavouritesRepository(prefs, scope)
            repository.toggle(OTHER)
            // playback/NotificationButtons toggles through its own store on the same file.
            FavouritesStore(prefs).toggle(TRACK)
            shadowOf(Looper.getMainLooper()).idle()
            val seen = withTimeout(5_000) { repository.favourites.first { TRACK in it } }
            assertEquals(setOf(TRACK, OTHER), seen)
            // And the next in-app toggle must not save a stale set over it.
            repository.toggle(OTHER)
            assertEquals(listOf(TRACK), FavouritesStore(prefs).all())
        }

    @Test
    fun theNewestFavouriteComesFirst() =
        runBlocking {
            val repository = SharedPrefsFavouritesRepository(prefs, scope)
            repository.toggle(OTHER)
            repository.toggle(TRACK)
            assertEquals(listOf(TRACK, OTHER), repository.favourites.value.toList())
        }

    private companion object {
        const val TRACK = "content://media/external/audio/media/42"
        const val OTHER = "content://media/external/audio/media/7"
    }
}
