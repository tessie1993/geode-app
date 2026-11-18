package dev.geode.ui

import dev.geode.data.CustomPalette
import dev.geode.data.SavedPaletteFiles
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The saved palettes never touch the disk on the caller's thread (composition, a tap): the store
 * is opened, read and written on the palette dispatcher, and the list follows when that has run.
 * What a palette file holds is native's (core/tests/look_files_test); libgeode is not loaded on the
 * JVM, so the store and the sanitize step are stand-ins here, and a palette's text is its id.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class SavedPalettesTest {
    private class Files : SavedPaletteFiles {
        val saved = mutableListOf<CustomPalette>()

        override fun list(): List<CustomPalette> = saved.toList()

        override fun save(palette: CustomPalette): CustomPalette = palette.also { saved += it }

        override fun delete(id: String) {
            saved.removeAll { it.text == id }
        }
    }

    private val asIs: (CustomPalette) -> CustomPalette = { it }

    @Test
    fun theStoreIsNotOpenedUntilTheDispatcherRuns() =
        runTest {
            val files = Files()
            var opened = 0
            val palettes =
                SavedPalettes(this, StandardTestDispatcher(testScheduler), asIs) {
                    opened++
                    files
                }

            val stored = palettes.save(CustomPalette("dusk"))
            assertEquals(0, opened)
            assertTrue(palettes.items.isEmpty())

            runCurrent()
            assertEquals(1, opened)
            assertEquals(listOf(stored), palettes.items)
            assertEquals(listOf(stored), files.list())
        }

    @Test
    fun aSaveThenADeleteLandInOrder() =
        runTest {
            val palettes = SavedPalettes(this, StandardTestDispatcher(testScheduler), asIs) { Files() }

            val kept = palettes.save(CustomPalette("dawn"))
            palettes.save(CustomPalette("noon"))
            palettes.delete("noon")
            runCurrent()

            assertEquals(listOf(kept), palettes.items)
        }

    @Test
    fun loadReadsWhatIsOnDisk() =
        runTest {
            val files = Files()
            val onDisk = files.save(CustomPalette("dusk"))
            val palettes = SavedPalettes(this, StandardTestDispatcher(testScheduler), asIs) { files }

            palettes.load()

            assertEquals(listOf(onDisk), palettes.items)
        }
}
