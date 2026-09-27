package dev.geode.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JSON is the app's untrusted boundary: a preset arrives inside a `geode://preset/` link that any
 * app or web page can send, and a document can carry a non-finite number without being malformed
 * JSON at all.
 *
 * The plainest route needs no trickery: `1e400` is an ordinary JSON number literal, and every
 * parser decodes it with `Double.parseDouble`, which overflows to `+Infinity` rather than
 * rejecting it. A quoted `"NaN"` is the second route, and how far it gets depends on the
 * implementation — Android's `org.json` converts strings with `Double.valueOf`, so it yields NaN
 * from `optDouble` as well as `getDouble`, whereas the stricter Maven `org.json` these tests run
 * against only lets it through `getDouble`. The assertions below are ones both implementations
 * satisfy, and the helpers close the boundary on either.
 *
 * A non-finite value that reaches `SceneParams` is not transient — the renderer's parameter
 * interpolation feeds its own output back in each frame, so one NaN poisons every interpolated
 * field, and the flash clamp cannot recover it because every comparison with a NaN is false.
 */
class FiniteJsonTest {
    @Test
    fun `an ordinary JSON literal really does decode to a non-finite value`() {
        // No quoting, nothing malformed: just a number too large for a double.
        assertTrue(JSONObject("""{"x":1e400}""").optDouble("x", 0.0).isInfinite())
        assertTrue(JSONObject("""{"x":1e400}""").getDouble("x").isInfinite())
        // And a quoted NaN survives getDouble even on the stricter parser.
        assertTrue(JSONObject("""{"x":"NaN"}""").getDouble("x").isNaN())
    }

    @Test
    fun `finiteDouble falls back instead of propagating a non-finite value`() {
        for (literal in listOf("1e400", "-1e400")) {
            val o = JSONObject("""{"x":$literal}""")
            assertEquals("$literal should fall back", 0.5, o.finiteDouble("x", 0.5), 1e-9)
        }
    }

    @Test
    fun `finiteDouble returns a finite value unchanged`() {
        assertEquals(0.75, JSONObject("""{"x":0.75}""").finiteDouble("x", 0.0), 1e-9)
        assertEquals(-3.0, JSONObject("""{"x":-3}""").finiteDouble("x", 0.0), 1e-9)
    }

    @Test
    fun `finiteDouble yields the fallback for an absent key`() {
        assertEquals(1.0, JSONObject("{}").finiteDouble("missing", 1.0), 1e-9)
    }

    @Test
    fun `finiteRequiredDouble substitutes its fallback for a non-finite value`() {
        assertEquals(0.25, JSONObject("""{"x":1e400}""").finiteRequiredDouble("x", 0.25), 1e-9)
        assertEquals(0.25, JSONObject("""{"x":"NaN"}""").finiteRequiredDouble("x", 0.25), 1e-9)
    }

    @Test
    fun `finiteRequiredDouble still throws when the key is absent`() {
        // Distinct from a non-finite value: a missing key is a malformed document, not a hostile
        // number, and the decoders rely on it failing loudly.
        assertThrows(org.json.JSONException::class.java) {
            JSONObject("{}").finiteRequiredDouble("missing")
        }
    }
}
