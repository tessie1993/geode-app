package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import java.io.File
import kotlin.math.roundToInt

/**
 * The scene params schema, as the shell sees it: every scene param the engine knows, with its
 * wire slot, kind, default, control range and whether a fade glides it.
 *
 * Every fact here is native (core/viz/ParamTable.inc, #346): the table is written once, in C++, and
 * the shell asks for it once through [GeodeNative.paramsSchemaText] and parses the line table it gets
 * back. This is the one place Kotlin learns a param's name, kind, default, range or slot: [SceneParams]
 * finds a name's slot by it and the slider and keyframe ranges read it (`model/ControlRange` in :app), so
 * nothing in Kotlin keeps a second copy and a new row needs no Kotlin edit. The pickers' and palettes' labels come with it.
 *
 * JVM unit tests cannot load libgeode. The build sets the [TEXT_FILE_PROPERTY] system property on
 * every unit-test task to `core/tests/params_schema.txt`, a checked-in copy of the table that the
 * native `params_schema_test` proves byte-equal to what C++ emits, so the tests read the same schema
 * the device does. On a device the property is never set.
 */
object ParamSchema {
    /** The table version this shell reads; core/viz/Params.hpp's `kSchemaTextVersion`. */
    const val VERSION: Int = 4

    const val TEXT_FILE_PROPERTY: String = "geode.paramsSchema"

    private val text: String by lazy {
        val fixture = System.getProperty(TEXT_FILE_PROPERTY)
        if (fixture != null) File(fixture).readText() else GeodeNative.paramsSchemaText()
    }

    private val table: List<ParamSpec> by lazy { parse(text) }

    /** The built-in palettes' names, in `palette` index order (the table's `palette|index|name` rows). */
    val paletteNames: List<String> by lazy { text.lines().filter { it.startsWith("palette|") }.map { it.substringAfterLast('|') } }

    private val choiceLabels: Map<String, List<String>> by lazy {
        text
            .lines()
            .filter { it.startsWith("choice|") }
            .map { it.split('|', limit = 4) }
            .groupBy({ it[1] }, { it[3] })
    }

    /** The labels of picker [name]'s values, in index order (the table's `choice|name|index|label` rows); empty for no picker. */
    fun choices(name: String): List<String> = choiceLabels[name].orEmpty()

    private val byName: Map<String, ParamSpec> by lazy { table.associateBy { it.name } }

    /** Every param, in slot order: `params[i].slot == i`, and the `geode_viz_set_params` frame is this long. */
    val params: List<ParamSpec> get() = table

    /** The param named [name] (the C++ member, wire, preset JSON and Kotlin property name), or null. */
    operator fun get(name: String): ParamSpec? = byName[name]

    /** The param in wire slot [slot]; throws outside `0 until params.size`. */
    operator fun get(slot: Int): ParamSpec = table[slot]

    /**
     * Reads the `geode_params_schema_text` table: a `params|<version>|<count>` header, then one
     * `param|slot|name|KIND|default|min|max|FADE|HOLD|LIVE|RETIRED` row per slot, in slot order, then the
     * palette names ([paletteNames]) and the picker labels ([choices]). Throws on a
     * version this shell does not read, a row out of order or a count that disagrees, since a
     * misread schema would put every param in the wrong wire slot.
     */
    internal fun parse(text: String): List<ParamSpec> {
        val rows = text.lines().filter { it.startsWith("param") }.map { it.split('|') }
        val header = rows.firstOrNull().orEmpty()
        check(header.size == 3 && header[0] == "params" && header[1] == VERSION.toString()) {
            "native params schema is $header, this shell reads version $VERSION"
        }
        val params =
            rows.drop(1).mapIndexed { slot, f ->
                check(f.size == 9 && f[0] == "param" && f[1] == slot.toString()) { "params schema row $slot is $f" }
                val range = f[5].toFloat()..f[6].toFloat()
                ParamSpec(slot, f[2], ParamSpec.Kind.valueOf(f[3]), f[4].toFloat(), range, f[7] == "FADE", f[8] == "RETIRED")
            }
        check(params.size == header[2].toInt()) { "params schema has ${params.size} rows, its header says ${header[2]}" }
        return params
    }
}

/**
 * One schema row (core/viz/Params.hpp's `ParamSpec`). Ints and flags carry their [default] and [range]
 * as floats, as the wire does: a flag is 0..1, a picker its first..last index.
 */
data class ParamSpec(
    val slot: Int,
    val name: String,
    val kind: Kind,
    val default: Float,
    val range: ClosedFloatingPointRange<Float>,
    /** True when a param fade glides it; false when it snaps to the new value (every int and flag). */
    val fades: Boolean,
    /** True for a row nothing reads any more: it keeps its wire slot, always at its default, and is not saved. */
    val retired: Boolean = false,
) {
    /** CIRCULAR is a float that wraps: the LFO adds to it unclamped. */
    enum class Kind { FLOAT, INT, BOOL, CIRCULAR }
}

/**
 * One look's scene params, opaque: the `geode_viz_set_params` wire frame native fills (one float per
 * [ParamSchema] row, ints and flags as floats, slot order) and the id of the saved palette the frame's
 * own palette came from. Every default, range, clamp, rule and codec is native (core/viz/Params,
 * ParamsJson, ParamsPalette): the shell reads a value by its row's name to draw a control, writes the
 * value the user moved, and hands the frame across. A new ParamTable.inc row needs no Kotlin edit.
 * Ints and flags are kept as given; native rounds and reads them (`SceneParams::fromWire`).
 */
class SceneParams private constructor(
    private val frame: FloatArray,
    /** The saved palette (data's `CustomPalette.id`) the look's own palette came from; "" for none. */
    val customPaletteId: String,
) {
    /** Param [name] as the wire carries it; throws for a name with no live schema row. */
    operator fun get(name: String): Float = frame[slotOf(name)]

    fun int(name: String): Int = get(name).roundToInt()

    fun flag(name: String): Boolean = get(name) > 0.5f

    /** These params with [name] set to [value]; the saved palette's id kept. */
    fun with(
        name: String,
        value: Float,
    ): SceneParams = SceneParams(frame.copyOf().also { it[slotOf(name)] = value }, customPaletteId)

    fun with(
        name: String,
        value: Int,
    ): SceneParams = with(name, value.toFloat())

    fun with(
        name: String,
        on: Boolean,
    ): SceneParams = with(name, if (on) 1f else 0f)

    /** The frame, a copy, for a JNI call. */
    fun frame(): FloatArray = frame.copyOf()

    /** The frame into [out] (at least [FIELDS] long), for a per-frame push that keeps one array. */
    fun frameInto(out: FloatArray) {
        frame.copyInto(out)
    }

    /** Whether the look draws its own palette (a hue override or your colours): native's rule. */
    val usesCustomPalette: Boolean get() = GeodeNative.paramsUsesCustomPalette(frame)

    /** Your colours, packed 0xRRGGBB, set from the first. */
    val paletteStops: List<Int> get() = GeodeNative.paramsPaletteStops(frame).toList()

    /** The hue base of the look's palette: the override when set, else the built-in palette's (native). */
    val paletteBase: Float get() = GeodeNative.paramsPaletteBase(int("palette"), get("paletteBaseOverride"))

    val paletteRange: Float get() = GeodeNative.paramsPaletteRange(int("palette"), get("paletteRangeOverride"))

    /** The built-in palette again: overrides, stops and the saved palette's id gone. */
    fun withoutCustomPalette(): SceneParams = SceneParams(GeodeNative.paramsClearCustomPalette(frame), NO_CUSTOM_PALETTE)

    /** A hue gradient as the look's own palette, from saved palette [id] if any. */
    fun withGradient(
        base: Float,
        span: Float,
        id: String = NO_CUSTOM_PALETTE,
    ): SceneParams = SceneParams(GeodeNative.paramsApplyGradient(frame, base, span), id)

    /** Your [stops] (the first five) as the look's own palette, from saved palette [id] if any. */
    fun withStops(
        stops: List<Int>,
        id: String = NO_CUSTOM_PALETTE,
    ): SceneParams = SceneParams(GeodeNative.paramsApplyStops(frame, stops.toIntArray()), id)

    /** The same frame naming saved palette [id]. */
    fun withPaletteId(id: String): SceneParams = SceneParams(frame, id)

    /** Saved palette [paletteText] (its file's text) as the look's own palette, named [id] (native's apply rule). */
    fun withSavedPalette(
        paletteText: String,
        id: String,
    ): SceneParams = SceneParams(GeodeNative.savedPaletteApply(paletteText, frame), id)

    /** These params once saved palette [deletedId] is deleted: native says which id the look keeps. */
    fun forgetPalette(deletedId: String): SceneParams = withPaletteId(GeodeNative.savedPaletteKeptId(customPaletteId, deletedId))

    override fun equals(other: Any?): Boolean =
        other is SceneParams && other.customPaletteId == customPaletteId && other.frame.contentEquals(frame)

    override fun hashCode(): Int = 31 * frame.contentHashCode() + customPaletteId.hashCode()

    override fun toString(): String = "SceneParams(${frame.size} slots, palette '$customPaletteId')"

    companion object {
        const val NO_CUSTOM_PALETTE: String = ""

        /** The frame's float count: one per schema row. */
        val FIELDS: Int get() = ParamSchema.params.size

        /** Every row at its native default. */
        val DEFAULT: SceneParams by lazy { SceneParams(FloatArray(FIELDS) { ParamSchema[it].default }, NO_CUSTOM_PALETTE) }

        /**
         * A frame native wrote (a roll, a reset) as params. [base]'s saved palette id carries over only while the
         * frame still draws its own palette (native's [usesCustomPalette]): a saved palette IS its overrides.
         */
        fun fromFrame(
            values: FloatArray,
            base: SceneParams = DEFAULT,
        ): SceneParams {
            val frame = values.copyOf(FIELDS)
            val keep = base.customPaletteId.isNotEmpty() && GeodeNative.paramsUsesCustomPalette(frame)
            return SceneParams(frame, if (keep) base.customPaletteId else NO_CUSTOM_PALETTE)
        }

        /** A frame and its saved palette id as native read them (a preset, a template's look). */
        fun read(
            values: FloatArray,
            customPaletteId: String,
        ): SceneParams = SceneParams(values.copyOf(FIELDS), customPaletteId)

        private fun slotOf(name: String): Int {
            val spec = ParamSchema[name]
            require(spec != null && !spec.retired) { "no live scene param named $name in the params schema" }
            return spec.slot
        }
    }
}
