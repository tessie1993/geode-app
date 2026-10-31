package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MilkPresetMemoryTest {
    @Test
    fun nothingChosenRestoresNothing() {
        assertNull(MilkPresetMemory().restorePath)
    }

    @Test
    fun aRequestNotYetLoadedIsWhatComesBack() {
        val memory = MilkPresetMemory()
        memory.request("/milk/a.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun theLoadedPresetComesBackAfterARebuild() {
        val memory = MilkPresetMemory()
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun aLaterRequestThatNeverLoadedDoesNotReplaceTheLastGoodOne() {
        val memory = MilkPresetMemory()
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        memory.request("/milk/broken.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun autoAdvanceReportsALoadTheRendererNeverAskedFor() {
        val memory = MilkPresetMemory()
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        memory.loaded("/milk/b.milk")
        assertEquals("/milk/b.milk", memory.restorePath)
    }
}
