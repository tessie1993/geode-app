package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The tiers, the scale choices, the preset match, the first-run pick and the Liquid policy's fallback are
 * native (core/viz/frame/Quality.hpp, core/viz/liquid/LiquidMath), checked on the host by
 * core/tests/quality_test.cpp; a JVM test cannot load libgeode. What stays Kotlin is the policy's wire values.
 */
class PerformanceSettingsTest {
    @Test
    fun liquidPolicyKeepsItsWireValues() {
        assertEquals(listOf(0, 1, 2), LiquidHotPolicy.entries.map { it.native })
    }
}
