package dev.geode.ui

import android.app.Application
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.File

/**
 * The panel's words against core's ids (core/tests/panel_words.txt, which core/tests/customize_rows_test
 * proves equal to what C++ lists): every word core can name has a string resource here, the shell holds
 * none core does not name, and each string has as many placeholders as core sends arguments for it.
 */
@RunWith(RobolectricTestRunner::class)
class PanelWordsTest {
    private val words =
        File(System.getProperty("geode.panelWords")!!)
            .readLines()
            .filter { it.isNotEmpty() }
            .map { it.substringBefore(' ') to it.substringAfter(' ').toInt() }

    @Test
    fun everyWordCoreNamesHasAString() {
        assertEquals(words.map { it.first }, PanelWords.byId.keys.toList())
    }

    @Test
    fun everyStringHasThePlaceholdersCoreFills() {
        val resources = ApplicationProvider.getApplicationContext<Application>().resources
        words.forEach { (id, args) ->
            val text = resources.getString(PanelWords.byId.getValue(id))
            val placeholders = Regex("""%(\d+)\$s""").findAll(text).map { it.groupValues[1] }.toSet()
            assertEquals("placeholders in $id", (1..args).map { it.toString() }.toSet(), placeholders)
        }
    }
}
