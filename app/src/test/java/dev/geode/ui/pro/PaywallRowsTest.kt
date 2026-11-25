package dev.geode.ui.pro

import dev.geode.pro.ProFeature
import org.junit.Assert.assertTrue
import org.junit.Test

/** The paywall lists everything Pro opens: a new [ProFeature] without a comparison row fails here. */
class PaywallRowsTest {
    @Test
    fun `every Pro feature has a row in the comparison`() {
        val listed = COMPARE_ROWS.flatMap { it.features }.toSet()
        ProFeature.entries.forEach { assertTrue("${it.name} has no paywall row", it in listed) }
    }
}
