package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

/**
 * The panel's words against core's ids (core/tests/panel_words.txt, which core/tests/customize_rows_test
 * proves equal to what C++ lists): every word core can name has a string resource here, and the shell
 * holds none core does not name.
 */
class PanelWordsTest {
    @Test
    fun everyWordCoreNamesHasAString() {
        val ids = File(System.getProperty("geode.panelWords")!!).readLines().filter { it.isNotEmpty() }
        assertEquals(ids, PanelWords.byId.keys.toList())
    }
}
