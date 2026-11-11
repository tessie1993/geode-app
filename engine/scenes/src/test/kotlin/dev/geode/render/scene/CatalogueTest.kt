package dev.geode.render.scene

import dev.geode.render.fluid.FluidQuality
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
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
        assertEquals(244, Catalogue.styles.size)
    }

    @Test
    fun aRetiredStyleOpensOnItsReplacement() {
        assertEquals(SceneIds.POOL, Catalogue.resolve("water"))
        assertEquals(SceneIds.PLASMA, Catalogue.resolve(SceneIds.PLASMA))
        assertEquals("no_such_style", Catalogue.resolve("no_such_style"))
    }

    @Test
    fun everyStyleCarriesItsLookGroup() {
        // The look column sits between the kind and the per-style scopes; a shifted field would show
        // up here as a scope name read as a look, or Detail lost from a marched style.
        val looks = setOf("FLOWING", "LIVING", "PSYCHEDELIC", "FRACTALS", "SPACE", "FLY_THROUGH", "GLASS", "PATTERNS", "CLASSIC")
        Catalogue.styles.forEach { assertTrue(it.id, it.look in looks) }
        assertEquals(looks, Catalogue.styles.mapTo(mutableSetOf()) { it.look })
        assertEquals("FLOWING", Catalogue.style("silk_web")?.look)
        assertEquals("CLASSIC", Catalogue.style(SceneIds.MILKDROP)?.look)
        assertEquals("FLY_THROUGH", Catalogue.style("terra_ridge")?.look)
        assertEquals("PSYCHEDELIC", Catalogue.style("winter")?.look)
        assertEquals("FRACTALS", Catalogue.style("mandelbox_cathedral")?.look)
        assertEquals(setOf(ParamScope.MARCH_DETAIL), Catalogue.style("mandelbox_cathedral")?.scopes)
        assertEquals(emptySet<ParamScope>(), Catalogue.style("ribbon_storm")?.scopes)
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
        assertFalse(ParamScope.MARCH_DETAIL.appliesTo(SceneIds.OUTRUN_DRIVE))
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
    }
}
