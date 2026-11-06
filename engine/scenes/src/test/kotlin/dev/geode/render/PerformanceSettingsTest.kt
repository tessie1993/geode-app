package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The tiers and the first-run pick are native now (core/viz/frame/Quality.hpp), checked on the host
 * by core/tests/quality_test.cpp; a JVM test cannot load libgeode. What stays Kotlin is the Liquid
 * policy's wire values.
 */
class PerformanceSettingsTest {
    @Test
    fun liquidPolicyKeepsItsWireValues() {
        assertEquals(listOf(0, 1, 2), LiquidHotPolicy.entries.map { it.native })
        assertEquals(LiquidHotPolicy.FULL, LiquidHotPolicy.fromNative(1))
        assertEquals(LiquidHotPolicy.SIMPLIFY, LiquidHotPolicy.fromNative(99))
    }
}
