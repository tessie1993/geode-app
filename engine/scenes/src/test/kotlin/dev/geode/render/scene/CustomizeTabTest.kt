package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Which page rolls and resets which control, held against a slice of the engine's roll table. */
class CustomizeTabTest {
    private val rollKeys =
        listOf(
            "MOTION" to ParamKeys.SPEED,
            "MOTION" to ParamKeys.CAM_FLY,
            "MOTION" to ParamKeys.ENDLESS_ZOOM,
            "MOTION" to ParamKeys.TURBULENCE,
            "COLOR" to ParamKeys.PALETTE,
            "FX" to ParamKeys.VIGNETTE,
            "SHAPE" to ParamKeys.KALEIDOSCOPE,
            "FLUID" to ParamKeys.FLUID_CURL,
            "FLUID" to ParamKeys.FLOW_STRENGTH,
            "CYMATICS" to ParamKeys.GEOMETRY,
            "REACTIVITY" to ParamKeys.AUDIO_DRIVE,
        )

    @Test
    fun thePanelShowsSevenPagesByKindSimpleFirstProLast() {
        assertEquals(
            listOf(
                CustomizeTab.MACROS,
                CustomizeTab.LOOK,
                CustomizeTab.STYLE,
                CustomizeTab.MOTION,
                CustomizeTab.MUSIC,
                CustomizeTab.MODULATE,
                CustomizeTab.ADVANCED,
            ),
            CustomizeTab.PANEL,
        )
    }

    @Test
    fun everyRolledControlBelongsToExactlyOnePage() {
        val owners = CustomizeTab.PANEL.associateWith { it.ownedKeys(rollKeys) }
        rollKeys.forEach { (_, key) ->
            assertEquals(key, 1, owners.values.count { key in it })
        }
    }

    @Test
    fun pagesHandOffTheControlsTheyDoNotShow() {
        assertEquals(setOf(ParamKeys.PALETTE, ParamKeys.VIGNETTE), CustomizeTab.LOOK.ownedKeys(rollKeys))
        assertEquals(
            setOf(ParamKeys.KALEIDOSCOPE, ParamKeys.FLUID_CURL, ParamKeys.GEOMETRY),
            CustomizeTab.STYLE.ownedKeys(rollKeys),
        )
        assertEquals(
            setOf(ParamKeys.SPEED, ParamKeys.CAM_FLY, ParamKeys.ENDLESS_ZOOM, ParamKeys.TURBULENCE, ParamKeys.FLOW_STRENGTH),
            CustomizeTab.MOTION.ownedKeys(rollKeys),
        )
        assertEquals(setOf(ParamKeys.AUDIO_DRIVE), CustomizeTab.MUSIC.ownedKeys(rollKeys))
        assertTrue(CustomizeTab.MACROS.ownedKeys(rollKeys).isEmpty())
        assertTrue(CustomizeTab.MODULATE.ownedKeys(rollKeys).isEmpty())
        assertTrue(CustomizeTab.ADVANCED.ownedKeys(rollKeys).isEmpty())
    }

    @Test
    fun liquidPickersAreNeverRolledButStayWithTheFlowControls() {
        val pickers = listOf(ParamKeys.LIQUID_BORN, ParamKeys.LIQUID_AGE, ParamKeys.LIQUID_SURFACE)
        // Not in the roll table, so no page owns them for a roll.
        pickers.forEach { key ->
            assertTrue(key, CustomizeTab.PANEL.none { key in it.ownedKeys(rollKeys) })
        }
        // They travel with the other all-styles flow controls when a page is reset.
        assertTrue(CustomizeTab.ALL_STYLES_FLOW_KEYS.containsAll(pickers))
        assertTrue(CustomizeTab.MOTION.takes.containsAll(pickers))
        assertTrue(CustomizeTab.STYLE.gives.containsAll(pickers))
    }

    @Test
    fun engineGroupsKeepTheirNames() {
        // core/viz/ParamRandomizer and saved locks name these.
        listOf("SHAPE", "COLOR", "MOTION", "FX", "REACTIVITY", "SCENE", "FLUID", "CYMATICS").forEach { name ->
            val group = CustomizeTab.valueOf(name)
            assertEquals(listOf(group), group.groups)
        }
    }
}
