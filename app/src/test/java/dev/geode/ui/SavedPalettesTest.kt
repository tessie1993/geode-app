package dev.geode.ui

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import dev.geode.data.PaletteStore
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The saved palettes never touch the disk on the caller's thread (composition, a tap): the store
 * is opened, read and written on the palette dispatcher, and the list follows when that has run.
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class SavedPalettesTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun theStoreIsNotOpenedUntilTheDispatcherRuns() =
        runTest {
            var opened = 0
            val palettes =
                SavedPalettes(this, StandardTestDispatcher(testScheduler)) {
                    opened++
                    PaletteStore(context)
                }

            val stored = palettes.save(PaletteStore.create("Dusk", 0.2f, 0.4f))
            assertEquals(0, opened)
            assertTrue(palettes.items.isEmpty())

            runCurrent()
            assertEquals(1, opened)
            assertEquals(listOf(stored), palettes.items)
            assertEquals(listOf(stored), PaletteStore(context).list())
        }

    @Test
    fun aSaveThenADeleteLandInOrder() =
        runTest {
            val palettes = SavedPalettes(this, StandardTestDispatcher(testScheduler)) { PaletteStore(context) }

            val kept = palettes.save(PaletteStore.create("Dawn", 0.1f, 0.3f))
            val dropped = palettes.save(PaletteStore.create("Noon", 0.5f, 0.2f))
            palettes.delete(dropped.id)
            runCurrent()

            assertEquals(listOf(kept), palettes.items)
        }

    @Test
    fun loadReadsWhatIsOnDisk() =
        runTest {
            val onDisk = PaletteStore(context).save(PaletteStore.create("Dusk", 0.2f, 0.4f))
            val palettes = SavedPalettes(this, StandardTestDispatcher(testScheduler)) { PaletteStore(context) }

            palettes.load()

            assertEquals(listOf(onDisk), palettes.items)
        }
}
