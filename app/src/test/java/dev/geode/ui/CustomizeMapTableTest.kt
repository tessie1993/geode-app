package dev.geode.ui

import dev.geode.pro.FreeTier
import dev.geode.render.scene.ParamKeys
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Customize's ten categories as the panel reads them: core/tests/customize.txt, which
 * core/tests/customize_test proves byte-equal to the native map and checks against the roll table
 * (every rolled control placed exactly once). These hold the shell's side of the same promises.
 */
class CustomizeMapTableTest {
    private val sections = CustomizeMapTable.sections

    @Test
    fun everyControlIsPlacedExactlyOnce() {
        val all = sections.flatMap { it.controls }
        val twice = all.groupingBy { it }.eachCount().filterValues { it != 1 }
        assertTrue("placed more than once: $twice", twice.isEmpty())
    }

    @Test
    fun theRollGroupsOfTodaysPagesAllLand() {
        // A slice of the engine's TAB|key roll table, one or more per group, as Kotlin read it before #333.
        val rolled =
            listOf(
                "MOTION" to ParamKeys.SPEED,
                "MOTION" to ParamKeys.CAM_FLY,
                "MOTION" to ParamKeys.ENDLESS_ZOOM,
                "MOTION" to ParamKeys.TURBULENCE,
                "COLOR" to ParamKeys.PALETTE,
                "COLOR" to ParamKeys.POSTERIZE,
                "FX" to ParamKeys.VIGNETTE,
                "SHAPE" to ParamKeys.KALEIDOSCOPE,
                "SCENE" to ParamKeys.BLEND_PRESET_CHANGES,
                "FLUID" to ParamKeys.FLUID_CURL,
                "FLUID" to ParamKeys.FLOW_STRENGTH,
                "CYMATICS" to ParamKeys.GEOMETRY,
                "REACTIVITY" to ParamKeys.AUDIO_DRIVE,
                "REACTIVITY" to "Reaction DNA",
            )
        val placed = sections.flatMap { it.controls }.toSet()
        rolled.forEach { (group, key) -> assertTrue("$group|$key has a place", key in placed) }
    }

    @Test
    fun theCategoriesInPanelOrderEachWithAtMostOneBasicsFirst() {
        assertEquals(CustomizeCategory.entries, sections.map { it.category }.distinct())
        CustomizeCategory.entries.forEach { category ->
            val own = CustomizeMapTable.sectionsOf(category)
            val basics = own.filter { it.basics }
            if (category == CustomizeCategory.EXPERT || category == CustomizeCategory.ENGINE) {
                assertTrue("$category has no Basics", basics.isEmpty())
            } else {
                assertEquals("$category has one Basics", 1, basics.size)
                assertTrue("$category opens on its Basics", own.first().basics)
                assertTrue("$category Basics hold 1..4", basics.single().controls.size in 1..4)
            }
        }
    }

    @Test
    fun theFreeSetIsTheUnionOfTheBasics() {
        val union = sections.filter { it.basics }.flatMapTo(mutableSetOf()) { it.controls }
        assertEquals(union, FreeTier.freeCustomizeControls(CustomizeMapTable.basicControls))
        assertTrue(ParamKeys.PALETTE in union)
        assertTrue("@macros" in union)
        // Deeper controls stay Pro.
        listOf(ParamKeys.HUE_SHIFT, ParamKeys.FLUID_CURL, "@reactions", "@shader_code").forEach { key ->
            assertTrue("$key is Pro", key !in union)
        }
    }

    @Test
    fun everyGroupIsShortEnoughToScanOpen() {
        // Each section is a collapsible group in its tab (owner, 2026-11-12); core/tests/customize_test holds the same bound.
        sections.forEach { assertTrue("${it.path} holds ${it.controls.size}", it.controls.size <= GROUP_MAX) }

        fun section(path: String) = sections.single { it.path == path }
        assertEquals(listOf("Stirrers", "Stirrer speed"), section("SHAPE/stirrers").controls)
        assertEquals("Fluid: layers", section("ENGINE/fluid_layers").title)
    }

    @Test
    fun theOwnersLayoutHolds() {
        fun section(path: String) = sections.single { it.path == path }
        assertEquals(
            listOf(ParamKeys.PALETTE, ParamKeys.BRIGHTNESS, ParamKeys.SATURATION, ParamKeys.BLOOM),
            section("LOOK/basics").controls,
        )
        assertTrue(ParamKeys.HUE_SHIFT in section("LOOK/colour_detail").controls)
        assertEquals(listOf(ParamKeys.SPEED, ParamKeys.ZOOM, ParamKeys.ROTATION), section("MOTION/basics").controls)
        assertEquals(listOf(ParamKeys.CAM_PATH, ParamKeys.CAM_FLY), section("CAMERA/basics").controls)
        assertNull(CustomizeMapTable.sectionsOf(CustomizeCategory.EXPERT).firstOrNull { it.basics })
        assertEquals("Colour detail", section("LOOK/colour_detail").title)
    }

    private companion object {
        const val GROUP_MAX = 8
    }
}
