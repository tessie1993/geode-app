package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class CacheSweeperTest {
    @get:Rule val tmp = TemporaryFolder()

    private fun file(
        path: String,
        modified: Long,
    ): File =
        File(tmp.root, path).apply {
            parentFile?.mkdirs()
            writeText("x")
            setLastModified(modified)
        }

    @Test
    fun prefixNamesOnlyTopLevelFilesWithAScratchName() {
        val aac = file("geode_aac_1.bin", 1_000)
        val backup = file("backup42.zip", 1_000)
        file("gl-program-binaries/studio-inside", 1_000)
        file("image.png", 1_000)

        val found = CacheSweeper.withPrefix(tmp.root, listOf("geode_aac_", "studio-", "backup"))

        assertEquals(setOf(aac, backup), found.toSet())
    }

    @Test
    fun olderThanReachesIntoFoldersAndSparesNewerFiles() {
        val old = file("share/templates/a.json", 1_000)
        val fresh = file("share/templates/b.json", 9_000)

        val found = CacheSweeper.olderThan(File(tmp.root, "share"), 5_000)

        assertEquals(listOf(old), found)
        assertTrue(fresh.exists())
    }

    @Test
    fun beyondNewestKeepsTheNewestCountAndNamesTheRest() {
        val files = (1..5).map { file("share/s$it.json", it * 1_000L) }

        val found = CacheSweeper.beyondNewest(File(tmp.root, "share"), 2)

        assertEquals(setOf(files[0], files[1], files[2]), found.toSet())
    }

    @Test
    fun aMissingFolderOrAZeroKeepIsHandled() {
        val none = File(tmp.root, "absent")
        assertTrue(CacheSweeper.olderThan(none, Long.MAX_VALUE).isEmpty())
        assertTrue(CacheSweeper.beyondNewest(none, 3).isEmpty())
        assertTrue(CacheSweeper.withPrefix(none, listOf("x")).isEmpty())
        file("share/a", 1_000)
        assertEquals(1, CacheSweeper.beyondNewest(File(tmp.root, "share"), -1).size)
    }
}
