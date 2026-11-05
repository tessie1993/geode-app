package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.io.File

/**
 * The scene params schema, as the shell sees it: every [SceneParams] field the engine knows, with its
 * wire slot, kind, default, control range and whether a fade glides it.
 *
 * Every fact here is native (core/viz/ParamTable.inc, #346): the table is written once, in C++, and
 * the shell asks for it once through [GeodeNative.paramsSchemaText] and parses the line table it gets
 * back, the way [Catalogue] reads the style catalogue. This is the one place Kotlin learns a param's
 * name, kind, default, range or slot: the wire codec (`render/bridge/SceneParamsCodec`) packs by it,
 * and preset JSON and the slider and keyframe ranges are to read it too, so nothing in Kotlin keeps a
 * second copy that could drift. What stays Kotlin is what is not engine: labels and page placement.
 *
 * JVM unit tests cannot load libgeode. The build sets the [TEXT_FILE_PROPERTY] system property on
 * every unit-test task to `core/tests/params_schema.txt`, a checked-in copy of the table that the
 * native `params_schema_test` proves byte-equal to what C++ emits, so the tests read the same schema
 * the device does. On a device the property is never set.
 */
object ParamSchema {
    /** The table version this shell reads; core/viz/Params.hpp's `kSchemaTextVersion`. */
    const val VERSION: Int = 1

    const val TEXT_FILE_PROPERTY: String = "geode.paramsSchema"

    private val table: List<ParamSpec> by lazy {
        parse(System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.paramsSchemaText())
    }

    private val byName: Map<String, ParamSpec> by lazy { table.associateBy { it.name } }

    /** Every param, in slot order: `params[i].slot == i`, and the `geode_viz_set_params` frame is this long. */
    val params: List<ParamSpec> get() = table

    /** The param named [name] (the C++ member, wire, preset JSON and Kotlin property name), or null. */
    operator fun get(name: String): ParamSpec? = byName[name]

    /** The param in wire slot [slot]; throws outside `0 until params.size`. */
    operator fun get(slot: Int): ParamSpec = table[slot]

    /**
     * Reads the `geode_params_schema_text` table: a `params|<version>|<count>` header, then one
     * `param|slot|name|KIND|default|min|max|FADE` row per slot, in slot order. Throws on a version
     * this shell does not read, a row out of order or a count that disagrees, since a misread schema
     * would put every param in the wrong wire slot.
     */
    internal fun parse(text: String): List<ParamSpec> = schemaSpecs(schemaRows(text, "params|$VERSION"), "param")
}

/**
 * One schema row (core/viz/Params.hpp's `ParamSpec`; for a [ConfigSchema] row, [slot] is its group index). Ints and
 * flags carry their [default] and [range] as floats, as the wire does: a flag is 0..1, a picker its first..last index.
 */
data class ParamSpec(
    val slot: Int,
    val name: String,
    val kind: Kind,
    val default: Float,
    val range: ClosedFloatingPointRange<Float>,
    /** True when a param fade glides it; false when it snaps to the new value (every int and flag, every config). */
    val fades: Boolean,
) {
    /** CIRCULAR is a float that wraps: the LFO adds to it unclamped. */
    enum class Kind { FLOAT, INT, BOOL, CIRCULAR }
}
