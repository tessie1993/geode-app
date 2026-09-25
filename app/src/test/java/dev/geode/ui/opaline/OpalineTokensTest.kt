package dev.geode.ui.opaline

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.sqrt

/**
 * Pins the Kotlin port to `ui-system/Opaline-3D-Library`: themes and material families to
 * `src/materials.js`, the light rig and motion clock to `catalogue/design-tokens.json`.
 */
class OpalineTokensTest {
    private val library = File("../ui-system/Opaline-3D-Library")
    private val materialsJs by lazy { File(library, "src/materials.js").readText() }
    private val tokens by lazy { JSONObject(File(library, "catalogue/design-tokens.json").readText()) }

    @Test fun themesMatchMaterialsJs() {
        val themes = Regex("""(\w+):\{name:'(\w+)',([^}]*)\}""").findAll(materialsJs).associate { it.groupValues[1] to it.groupValues[3] }
        assertEquals(OpalineThemeId.entries.size, themes.size)
        for (theme in OpalineThemeId.entries) {
            val fields =
                Regex("""(\w+):'#([0-9a-f]{6})'""").findAll(themes.getValue(theme.name.lowercase())).associate {
                    it.groupValues[1] to (0xFF000000 or it.groupValues[2].toLong(16))
                }
            assertEquals("${theme.name} gel", fields["gel"], theme.gel)
            assertEquals("${theme.name} blue", fields["blue"], theme.blue)
            assertEquals("${theme.name} deep", fields["deep"], theme.deep)
            assertEquals("${theme.name} accent", fields["accent"], theme.accent)
            assertEquals("${theme.name} water", fields["water"], theme.water)
            assertEquals("${theme.name} stone", fields["stone"], theme.stone)
            assertEquals("${theme.name} leaf", fields["leaf"], theme.leaf)
            assertEquals("${theme.name} background", fields["background"], theme.background)
        }
    }

    @Test fun themesMatchDesignTokens() {
        val list = tokens.getJSONArray("themes")
        assertEquals(OpalineThemeId.entries.size, list.length())
        for (i in 0 until list.length()) {
            val entry = list.getJSONObject(i)
            val theme = OpalineThemeId.valueOf(entry.getString("id").uppercase())
            assertEquals(hex(entry.getString("accent")), theme.accent)
            assertEquals(hex(entry.getString("environment")), theme.background)
            assertEquals(hex(entry.getString("nature")), theme.leaf)
        }
    }

    @Test fun materialFamiliesMatchMaterialsJs() {
        for (material in OpalineMaterial.entries) {
            val family = material.name.lowercase()
            val match =
                Regex("""$family:physical\('$family',\{([^}]*)\},t(?:,\{([^}]*)\})?\)""").find(materialsJs)
                    ?: error("no $family in materials.js")
            val physical = parse(match.groupValues[1])
            val shader = parse(match.groupValues[2])
            fun check(
                label: String,
                source: Map<String, Pair<Float, Float>>,
                key: String,
                default: Float,
                tidal: Float,
                opal: Float,
            ) {
                val (plain, pearl) = source[key] ?: (default to default)
                assertEquals("$family $label", plain, tidal, 1e-6f)
                assertEquals("$family $label (Opal)", pearl, opal, 1e-6f)
            }
            val t = OpalineThemeId.TIDAL
            val o = OpalineThemeId.OPAL
            check("roughness", physical, "roughness", 1f, material.roughness(t), material.roughness(o))
            check("clearcoat", physical, "clearcoat", 0f, material.clearcoat, material.clearcoat)
            check("clearcoatRoughness", physical, "clearcoatRoughness", 0f, material.clearcoatRoughness(t), material.clearcoatRoughness(o))
            // MeshPhysicalMaterial's default IOR is 1.5; stone does not set one.
            check("ior", physical, "ior", 1.5f, material.ior, material.ior)
            check("iridescence", physical, "iridescence", 0f, material.iridescence, material.iridescence)
            check("cloud", shader, "cloud", 0f, material.cloud(t), material.cloud(o))
            check("flow", shader, "flow", 0f, material.flow, material.flow)
            check("grain", shader, "grain", 0f, material.grain, material.grain)
        }
    }

    @Test fun lightRigAndClockMatchDesignTokens() {
        val direction = tokens.getJSONObject("lighting").getJSONArray("keyDirection")
        val x = direction.getDouble(0).toFloat()
        val y = direction.getDouble(1).toFloat()
        val z = direction.getDouble(2).toFloat()
        val length = sqrt(x * x + y * y + z * z)
        // Tokens are +Y up; the renderer works in screen space, +Y down.
        assertEquals(x / length, OpalineLight.key[0], 1e-6f)
        assertEquals(-y / length, OpalineLight.key[1], 1e-6f)
        assertEquals(z / length, OpalineLight.key[2], 1e-6f)
        assertEquals(tokens.getJSONObject("motion").getDouble("fixedDt").toFloat(), OpalineMotion.FIXED_DT, 1e-9f)
    }

    @Test fun textOnEveryBodyMeetsWcagAa() {
        for (theme in OpalineThemeId.entries) {
            val p = theme.palette
            assertAtLeast("${theme.name} onGel/gel", 4.5f, p.onGel, p.gel)
            assertAtLeast("${theme.name} text/slab", 4.5f, p.text, p.slab)
            assertAtLeast("${theme.name} muted/slab", 4.5f, p.textMuted.compositeOver(p.slab), p.slab)
            assertAtLeast("${theme.name} text/environment", 4.5f, p.text, p.environment)
            assertAtLeast("${theme.name} muted/environment", 4.5f, p.textMuted.compositeOver(p.environment), p.environment)
            assertAtLeast("${theme.name} accent/slab", 3f, p.accent, p.slab)
            assertAtLeast("${theme.name} error/slab", 4.5f, p.error, p.slab)
        }
    }

    private fun assertAtLeast(
        label: String,
        minimum: Float,
        foreground: Color,
        background: Color,
    ) {
        val ratio = OpalinePalette.contrast(foreground, background)
        assertTrue("$label contrast $ratio < $minimum", ratio >= minimum)
    }

    /** `key:value` pairs; `key:pearl?a:b` yields (b, a): the value, then the Opal value. */
    private fun parse(block: String): Map<String, Pair<Float, Float>> =
        Regex("""(\w+):(pearl\?)?(-?[0-9]*\.?[0-9]+)(?::(-?[0-9]*\.?[0-9]+))?""").findAll(block).associate { m ->
            val first = m.groupValues[3].toFloat()
            if (m.groupValues[2].isNotEmpty()) {
                m.groupValues[1] to (m.groupValues[4].toFloat() to first)
            } else {
                m.groupValues[1] to (first to first)
            }
        }

    private fun hex(value: String): Long = 0xFF000000 or value.removePrefix("#").toLong(16)
}
