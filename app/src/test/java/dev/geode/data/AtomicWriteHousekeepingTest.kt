package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class AtomicWriteHousekeepingTest {
    @get:Rule val tmp = TemporaryFolder()

    @Test
    fun locksAreAFixedSetWhateverHowManyPathsAreWritten() {
        val paths = (0 until 10_000).map { "/data/files/presets/preset-$it.json" }
        assertSame(AtomicWrite.lockFor(paths[0]), AtomicWrite.lockFor(paths[0]))
        val distinct = paths.map { System.identityHashCode(AtomicWrite.lockFor(it)) }.toSet()
        assertTrue(distinct.size <= 64)
    }

    @Test
    fun onlyTempsWrittenBeforeThisProcessAreStale() {
        val startedAt = 1_000_000L
        val old = File(tmp.root, "presets/a.json.tmp").apply { parentFile?.mkdirs() }
        old.writeText("killed")
        old.setLastModified(startedAt - 1_000)
        val running = File(tmp.root, "b.json.tmp").apply { writeText("being written") }
        running.setLastModified(startedAt + 1_000)
        val kept = File(tmp.root, "c.json").apply { writeText("kept") }
        kept.setLastModified(startedAt - 1_000)

        assertEquals(listOf(old), AtomicWrite.staleTemps(tmp.root, startedAt))
    }

    @Test
    fun aWriteStillLandsWithStripedLocks() {
        val file = File(tmp.root, "x/y.json")
        assertTrue(AtomicWrite.text(file, "{}"))
        assertEquals("{}", file.readText())
        assertTrue(File(tmp.root, "x").listFiles().orEmpty().none { it.name.endsWith(AtomicWrite.TEMP_SUFFIX) })
    }
}
