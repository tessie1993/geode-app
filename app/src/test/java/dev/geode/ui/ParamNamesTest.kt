package dev.geode.ui

import dev.geode.model.SceneParams
import dev.geode.model.TestParams
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Customize's screens name scene params as text (`SceneParams.get`/`with`). Their controls are declared natively
 * now (core/tests/customize_rows_test holds those names), so the Kotlin screens pass none to scan; what is left
 * here is the keyed edit itself.
 */
class ParamNamesTest {
    init {
        TestParams.install()
    }

    @Test
    fun aKeyedEditIsTheFieldsCopy() {
        val p = SceneParams.DEFAULT.withPaletteId("kept")
        assertEquals("kept", p.with("speed", 1.5f).customPaletteId)
        assertEquals(1.5f, p.with("speed", 1.5f)["speed"], 0f)
        assertEquals(SceneParams.DEFAULT["speed"], p["speed"], 0f)
    }
}
