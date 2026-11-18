package dev.geode.model

import java.io.File
import kotlin.math.roundToInt

/**
 * The scene params for JVM tests, which cannot load the native library: the rows of
 * `core/tests/params_schema.txt` (the build names it in the `geode.paramsSchema` system property, and the
 * native `params_schema_test` proves it equal to what C++ emits), answering [SceneParams.Reads] as native
 * does. A test that reaches [SceneParams] calls [install] first, or [TestCatalogue.install], which does.
 */
object TestParams : SceneParams.Reads {
    private class Row(
        val slot: Int,
        val kind: String,
        val default: Float,
        val live: Boolean,
    )

    private val rows: Map<String, Row> by lazy {
        File(checkNotNull(System.getProperty("geode.paramsSchema")))
            .readLines()
            .filter { it.startsWith("param|") }
            .map { it.split('|') }
            .associate { f -> f[2] to Row(f[1].toInt(), f[3], f[4].toFloat(), f[8] == "LIVE") }
    }

    fun install() = SceneParams.install(this)

    /** True for a name with a row nothing has retired. */
    fun live(name: String): Boolean = rows[name]?.live == true

    override fun defaults(): FloatArray = FloatArray(rows.size).also { frame -> rows.values.forEach { frame[it.slot] = it.default } }

    override fun get(
        frame: FloatArray,
        name: String,
    ): Float = rows[name]?.takeIf { it.live }?.let { frame[it.slot] } ?: Float.NaN

    override fun set(
        frame: FloatArray,
        name: String,
        value: Float,
    ): FloatArray? {
        val row = rows[name]?.takeIf { it.live } ?: return null
        val kept =
            when (row.kind) {
                "INT" -> value.roundToInt().toFloat()
                "BOOL" -> if (value > 0.5f) 1f else 0f
                else -> value
            }
        return frame.copyOf().also { it[row.slot] = kept }
    }

    override fun int(
        frame: FloatArray,
        name: String,
    ): Int = get(frame, name).let { if (it.isNaN()) 0 else it.roundToInt() }

    override fun flag(
        frame: FloatArray,
        name: String,
    ): Boolean = get(frame, name) > 0.5f

    /** Native's rule on the same rows: the id stays while a hue override or the first stop is set. */
    override fun paletteIdOn(
        frame: FloatArray,
        lookId: String,
    ): String {
        val own = get(frame, "paletteBaseOverride") >= 0f || get(frame, "paletteRangeOverride") >= 0f || get(frame, "paletteStop0") >= 0f
        return if (own) lookId else ""
    }
}
