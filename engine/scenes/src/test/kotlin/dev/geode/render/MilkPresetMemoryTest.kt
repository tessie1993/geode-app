package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MilkPresetMemoryTest {
    private class FakeStore(
        var saved: String? = null,
        val files: MutableSet<String> = mutableSetOf("/milk/a.milk", "/milk/b.milk", "/milk/broken.milk", BUNDLED),
    ) : MilkPresetMemory.Store {
        override fun read(): String? = saved

        override fun write(path: String?) {
            saved = path
        }

        override fun usable(path: String): Boolean = path in files

        override fun firstRun(): String? = BUNDLED.takeIf { it in files }
    }

    @Test
    fun aFirstLaunchStartsOnTheBundledPresetNotTheIdleOne() {
        assertEquals(BUNDLED, MilkPresetMemory(FakeStore()).restorePath)
    }

    @Test
    fun nothingAtAllRestoresNothing() {
        assertNull(MilkPresetMemory(FakeStore(files = mutableSetOf())).restorePath)
    }

    @Test
    fun aRequestNotYetLoadedIsWhatComesBack() {
        val memory = MilkPresetMemory(FakeStore())
        memory.request("/milk/a.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun theLoadedPresetComesBackAfterARebuild() {
        val memory = MilkPresetMemory(FakeStore())
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun theLoadedPresetSurvivesARestart() {
        val store = FakeStore()
        MilkPresetMemory(store).loaded("/milk/b.milk")
        val afterRestart = MilkPresetMemory(store)
        assertEquals("/milk/b.milk", afterRestart.lastShown)
        assertEquals("/milk/b.milk", afterRestart.restorePath)
    }

    @Test
    fun aRequestThatNeverLoadedIsNotSaved() {
        val store = FakeStore()
        MilkPresetMemory(store).request("/milk/a.milk")
        assertNull(store.saved)
    }

    @Test
    fun aLaterRequestThatNeverLoadedDoesNotReplaceTheLastGoodOne() {
        val memory = MilkPresetMemory(FakeStore())
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        memory.request("/milk/broken.milk")
        assertEquals("/milk/a.milk", memory.restorePath)
    }

    @Test
    fun autoAdvanceReportsALoadTheRendererNeverAskedFor() {
        val memory = MilkPresetMemory(FakeStore())
        memory.request("/milk/a.milk")
        memory.loaded("/milk/a.milk")
        memory.loaded("/milk/b.milk")
        assertEquals("/milk/b.milk", memory.restorePath)
    }

    @Test
    fun aSavedPresetWhoseFileIsGoneFallsBackToTheBundledOne() {
        val store = FakeStore(saved = "/milk-builtin/old.milk")
        val memory = MilkPresetMemory(store)
        assertNull(memory.lastShown)
        assertEquals(BUNDLED, memory.restorePath)
    }

    @Test
    fun aDeletedPresetIsForgottenOnDiskToo() {
        val store = FakeStore()
        val memory = MilkPresetMemory(store)
        memory.loaded("/milk/a.milk")
        memory.forget(listOf("/milk/a.milk"))
        assertNull(store.saved)
        assertEquals(BUNDLED, memory.restorePath)
    }

    private companion object {
        const val BUNDLED = "/files/milk/geode_bloom.milk"
    }
}
