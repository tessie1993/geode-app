package dev.geode.render.scene

import dev.geode.render.fluid.FluidQuality
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.lang.reflect.Modifier
import java.lang.reflect.Modifier

/**
 * The shell's reading of the native catalogue. These run on the checked-in copy of the table
 * (core/tests/catalogue.txt, which the native catalogue_test proves equal to what C++ emits), so a
 * failure here is the shell and the engine disagreeing about a name.
 */
class CatalogueTest {
    private val sceneIds: List<String> =
        SceneIds::class.java.declaredFields
            .filter { it.type == String::class.java && Modifier.isStatic(it.modifiers) }
            .map { it.get(null) as String }

    private val paramKeys: List<String> =
        ParamKeys::class.java.declaredFields
            .filter { it.type == String::class.java && Modifier.isStatic(it.modifiers) }
            .map { it.get(null) as String }

    @Test
    fun everyPersistedSceneIdIsACatalogueStyle() {
        sceneIds.forEach { id -> assertTrue(id, Catalogue.style(id) != null) }
        assertEquals("silk_web", Catalogue.styles.first().id)
        assertEquals(SceneKind.SILK, Catalogue.kindOf(SceneIds.DEFAULT))
        assertEquals(SceneKind.SILK, Catalogue.kindOf("no_such_style"))
        assertEquals(224, Catalogue.styles.size)
    }

    @Test
    fun everyDedicatedRendererStyleHasALabel() {
        val labelled = setOf(SceneKind.SILK, SceneKind.LIFE, SceneKind.MYCELIUM, SceneKind.ACID, SceneKind.FLUID, SceneKind.CYMATICS)
        Catalogue.styles.filter { it.kind in labelled }.forEach { style ->
            assertNotEquals(style.id, style.id, VisualStyleCatalog.label(style.id))
        }
        StyleFamilies.all.forEach { assertNotEquals(it.name, it.name, it.label) }
        assertEquals("Original · Dye", VisualStyleCatalog.label(SceneIds.FLUID))
    }

    @Test
    fun everyScopedControlIsAParamKey() {
        // The native table lists 80 controls that are not universal; all of them must be the same
        // strings Kotlin labels, locks and searches by, or a control would silently go universal.
        assertEquals(80, paramKeys.count { ParamScope.of(it) != ParamScope.UNIVERSAL })
        assertEquals(ParamScope.MARCH_DETAIL, ParamScope.of(ParamKeys.MARCH_DETAIL))
        assertEquals(ParamScope.UNIVERSAL, ParamScope.of(ParamKeys.ZOOM))
    }

    @Test
    fun scopesReachTheStylesTheyShould() {
        assertTrue(ParamScope.MARCH_DETAIL.appliesTo(SceneIds.VANISHING))
        assertFalse(ParamScope.MARCH_DETAIL.appliesTo(SceneIds.PLASMA))
        assertTrue(ParamScope.FLUID_SIM.appliesTo("fluid_ink"))
        assertFalse(ParamScope.FLOW_FIELD_SIM.appliesTo(SceneIds.FLUID))
        assertFalse(ParamScope.SCENE_CLOCK.appliesTo(SceneIds.MILKDROP))
        assertTrue(ParamScope.TRAIL_TOGGLE.appliesTo("no_such_style"))
    }

    @Test
    fun rangesAndValuesTheControlsQuote() {
        assertEquals(0.25f..1.5f, Catalogue.range(ParamKeys.MARCH_DETAIL))
        assertEquals(40f..440f, Catalogue.range(ParamKeys.FUNDAMENTAL_HZ))
        assertEquals(1f..8f, Catalogue.range(ParamKeys.STANDING_WAVES))
        assertEquals(1f, Catalogue.override("kundt_tube", ParamKeys.GEOMETRY))
        assertEquals(null, Catalogue.override(SceneIds.CYMATICS, ParamKeys.GEOMETRY))
        assertEquals(FluidQuality.LABELS.size, Catalogue.value("fluidTiers").toInt())
        assertEquals(3f, TouchTransform.zoom(2f, 10f))
        assertEquals(0.3f, TouchTransform.zoom(1f, 0.01f))
        assertEquals(1.2f, TouchTransform.rotation(0f, 100f), 1e-5f)
    }
}
