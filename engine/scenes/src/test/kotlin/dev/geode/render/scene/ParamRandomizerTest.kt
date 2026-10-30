package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.lang.reflect.Modifier

class ParamRandomizerTest {
    private val fields =
        SceneParams::class.java.declaredFields
            .filterNot { Modifier.isStatic(it.modifiers) }
            .onEach { it.isAccessible = true }

    /** Every field moved off its default. */
    private fun everythingChanged(): SceneParams {
        val p = SceneParams.DEFAULT.copy()
        fields.forEach { f ->
            when (f.type) {
                Float::class.javaPrimitiveType -> f.setFloat(p, f.getFloat(p) + 0.123f)
                Int::class.javaPrimitiveType -> f.setInt(p, f.getInt(p) + 1)
                Boolean::class.javaPrimitiveType -> f.setBoolean(p, !f.getBoolean(p))
                else -> f.set(p, "changed")
            }
        }
        return p
    }

    @Test
    fun everyFieldIsResetBySomeTabOrSaysWhyNot() {
        // The audit's claim, made checkable: a field no tab rolls must be listed in
        // NEVER_ROLLED (or NOT_RENDERED) with a reason, or "Reset tab" silently misses it.
        var p = everythingChanged()
        CustomizeTab.entries.forEach { p = ParamRandomizer.resetTab(p, it) }
        val untouched = fields.filter { it.get(p) != it.get(SceneParams.DEFAULT) }.map { it.name }
        val excused = ParamRandomizer.NEVER_ROLLED.keys + SceneParams.NOT_RENDERED.keys
        assertEquals(emptyList<String>(), untouched - excused)
    }

    @Test
    fun inertParamsAreNeverRolled() {
        listOf("pulse", "shake", "beatResponse", "flash", "strobe", "formDrive").forEach {
            assertTrue(it, it in ParamRandomizer.NEVER_ROLLED)
        }
    }

    @Test
    fun rollsStayUnderTheFlashClamp() {
        repeat(200) { seed ->
            val p = ParamRandomizer.randomize(SceneParams.DEFAULT, emptySet(), kotlin.random.Random(seed))
            assertTrue(p.bloom <= 0.25f && p.glitch <= 0.25f)
            assertTrue(p.brightness <= 1.25f && p.contrast <= 1.25f && p.intensity <= 1.25f)
        }
    }
}
