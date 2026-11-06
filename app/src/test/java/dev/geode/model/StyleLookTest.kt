package dev.geode.model

import dev.geode.render.scene.Catalogue
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The Styles page matches the native catalogue's look column to [StyleLook] by name, so the two
 * lists must stay the same: a group renamed on one side only would drop its styles into the
 * fallback group. Reads the checked-in catalogue table (core/tests/catalogue.txt), as every JVM
 * test does.
 */
class StyleLookTest {
    @Test
    fun everyCatalogueLookIsAStyleLookAndEveryGroupHasStyles() {
        val native = Catalogue.styles.mapTo(mutableSetOf()) { it.look }
        assertEquals(StyleLook.entries.mapTo(mutableSetOf()) { it.name }, native)
    }
}
