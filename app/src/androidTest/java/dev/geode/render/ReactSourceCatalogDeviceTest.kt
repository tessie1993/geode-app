package dev.geode.render

import androidx.test.ext.junit.runners.AndroidJUnit4
import dev.geode.engine.bridge.GeodeNative
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * C++ owns the reaction source catalogue (core/viz/Reactions.cpp) and Customize reads it alone, through
 * the modulation catalogue's `react|ordinal|NAME|Label|GROUP|0/1` rows. On the device those rows must
 * arrive whole: in wire order, each with words to show and a group the catalogue also lists.
 */
@RunWith(AndroidJUnit4::class)
class ReactSourceCatalogDeviceTest {
    @Test
    fun theNativeCatalogueListsEverySourceInWireOrder() {
        val rows = GeodeNative.modCatalogText().lines().map { it.split('|') }
        val groups = rows.filter { it[0] == "group" }.map { it[2] }
        val sources = rows.filter { it[0] == "react" }
        assertTrue("the catalogue should list reaction sources", sources.isNotEmpty())
        assertTrue("the catalogue should list source groups", groups.isNotEmpty())
        sources.forEachIndexed { i, row ->
            assertEquals(row.toString(), 6, row.size)
            assertEquals(row.toString(), i, row[1].toInt())
            assertTrue(row.toString(), row[2].isNotBlank() && row[3].isNotBlank())
            assertTrue(row.toString(), row[4] in groups)
            assertTrue(row.toString(), row[5] == "0" || row[5] == "1")
        }
        assertEquals(sources.size, sources.map { it[2] }.distinct().size)
    }
}
