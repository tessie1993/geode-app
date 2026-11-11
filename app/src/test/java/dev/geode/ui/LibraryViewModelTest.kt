package dev.geode.ui

import androidx.lifecycle.SavedStateHandle
import dev.geode.data.LibraryState
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import kotlin.coroutines.ContinuationInterceptor

/**
 * The library's browse pipeline on test dispatchers: a 15k-track library, a separate "compute"
 * dispatcher standing in for Dispatchers.Default, and Main as its own dispatcher, so the test can
 * tell where each pass ran and how many ran.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class LibraryViewModelTest {
    private val main = StandardTestDispatcher(name = "main")

    @Before
    fun setUp() = Dispatchers.setMain(main)

    @After
    fun tearDown() = Dispatchers.resetMain()

    private val tracks =
        List(LIBRARY_SIZE) { i ->
            DeviceTrack(
                uri = "content://track/$i",
                title = if (i == BEYONCE_AT) "Halo" else "Song $i",
                artist = if (i == BEYONCE_AT) "Beyoncé" else "Artist ${i % 400}",
                album = "Album ${i % 1200}",
                folder = "/music/${i % 50}",
                durationMs = 180_000L + i,
                addedSec = i.toLong(),
            )
        }

    private class Run(
        val query: String,
        val dispatcher: CoroutineDispatcher?,
    )

    private fun viewModel(
        compute: CoroutineDispatcher,
        runs: MutableList<Run>,
        deviceTracks: MutableStateFlow<List<DeviceTrack>> = MutableStateFlow(tracks),
        loaded: MutableStateFlow<Boolean> = MutableStateFlow(true),
    ): LibraryViewModel =
        LibraryViewModel(
            sessionRef = { error("the browse pipeline must not need a session") },
            inputs =
                LibraryInputs(
                    deviceTracks = deviceTracks,
                    deviceTracksLoaded = loaded,
                    library = MutableStateFlow(LibraryState()),
                    overrides = MutableStateFlow(emptyMap<String, LibraryTrack>()),
                    scanning = MutableStateFlow(false),
                ),
            savedStateHandle = SavedStateHandle(),
            compute = compute,
            browseFn = { index, query, sort ->
                runs += Run(query, currentCoroutineContext()[ContinuationInterceptor] as? CoroutineDispatcher)
                LibraryBrowse.browse(index, query, sort)
            },
        )

    @Test
    fun groupingRunsOnTheComputeDispatcherNotMain() =
        runTest(main) {
            val compute = StandardTestDispatcher(testScheduler, name = "compute")
            val runs = mutableListOf<Run>()
            val vm = viewModel(compute, runs)
            backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.uiState.collect {} }
            advanceUntilIdle()

            val state = vm.uiState.value
            assertEquals(LIBRARY_SIZE, state.tracks.size)
            assertEquals(1200, state.browse.albums.size)
            assertEquals(401, state.browse.artists.size)
            assertEquals(50, state.browse.folders.size)
            assertTrue(runs.isNotEmpty())
            runs.forEach { assertSame("browse ran off its dispatcher", compute, it.dispatcher) }
        }

    @Test
    fun aBurstOfKeystrokesFiltersOnce() =
        runTest(main) {
            val compute = StandardTestDispatcher(testScheduler, name = "compute")
            val runs = mutableListOf<Run>()
            val vm = viewModel(compute, runs)
            backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.uiState.collect {} }
            advanceUntilIdle()
            runs.clear()

            // Typed faster than the debounce: each key lands well inside the window.
            for (typed in listOf("b", "be", "bey", "beyo", "beyon", "beyonc", "beyonce")) {
                vm.setQuery(typed)
                advanceTimeBy(QUERY_DEBOUNCE_MS / 5)
            }
            // The field shows every keystroke at once; the list has not moved yet.
            assertEquals("beyonce", vm.query.value)
            advanceUntilIdle()

            assertEquals(listOf("beyonce"), runs.map { it.query })
            val state = vm.uiState.value
            assertEquals("beyonce", state.query)
            assertEquals(listOf("content://track/$BEYONCE_AT"), state.tracks.map { it.uri })
            runs.forEach { assertSame(compute, it.dispatcher) }
        }

    @Test
    fun clearingTheBoxAnswersWithoutWaiting() =
        runTest(main) {
            val compute = StandardTestDispatcher(testScheduler, name = "compute")
            val runs = mutableListOf<Run>()
            val vm = viewModel(compute, runs)
            backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.uiState.collect {} }
            vm.setQuery("halo")
            advanceUntilIdle()
            assertEquals(1, vm.uiState.value.tracks.size)

            vm.setQuery("")
            // Well short of the debounce: a cleared box must not wait it out.
            advanceTimeBy(QUERY_DEBOUNCE_MS / 2)
            runCurrent()
            assertEquals(LIBRARY_SIZE, vm.uiState.value.tracks.size)
        }

    @Test
    fun duplicatesAreFoundOffMainOverTheWholeLibrary() =
        runTest(main) {
            val compute = StandardTestDispatcher(testScheduler, name = "compute")
            val copy = tracks[7].copy(uri = "content://track/copy", folder = "/download")
            val vm = viewModel(compute, mutableListOf(), deviceTracks = MutableStateFlow(tracks + copy))
            backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.duplicates.collect {} }
            vm.setQuery("halo")
            advanceUntilIdle()
            // Song 7 is not in the search, and its copy is still found.
            assertEquals(1, vm.duplicates.value.size)
            val group = vm.duplicates.value.single()
            assertEquals(2, group.tracks.size)
        }

    @Test
    fun emptyLibraryWaitsForTheFirstQuery() =
        runTest(main) {
            val compute = StandardTestDispatcher(testScheduler, name = "compute")
            val loaded = MutableStateFlow(false)
            val vm = viewModel(compute, mutableListOf(), deviceTracks = MutableStateFlow(emptyList()), loaded = loaded)
            backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.uiState.collect {} }
            advanceUntilIdle()
            assertFalse("not asked yet is not empty", vm.uiState.value.showEmptyLibrary)

            loaded.value = true
            advanceUntilIdle()
            assertTrue(vm.uiState.value.showEmptyLibrary)

            vm.setQuery("anything")
            advanceUntilIdle()
            assertFalse("a search with no hits is not an empty library", vm.uiState.value.showEmptyLibrary)
        }

    private companion object {
        const val LIBRARY_SIZE = 15_000
        const val BEYONCE_AT = 4_321
    }
}
