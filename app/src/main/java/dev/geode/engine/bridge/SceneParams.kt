package dev.geode.engine.bridge

import kotlin.math.roundToInt

/**
 * One look's scene params, opaque: the `geode_viz_set_params` frame native fills and the id of the saved
 * palette the frame's own palette came from. The shell knows no param: which exist, their slots, kinds,
 * defaults, ranges, clamps and codecs are native (core/viz/Params, ParamTable.inc, ParamsJson,
 * ParamsPalette). To draw a control it asks native for a value by the control's name, to move one it asks
 * native for the edited frame, and it hands the frame across; a new ParamTable.inc row needs no Kotlin edit.
 */
class SceneParams private constructor(
    private val frame: FloatArray,
    /** The saved palette (data's `CustomPalette.id`) the look's own palette came from; "" for none. */
    val customPaletteId: String,
) {
    /** Param [name] as native reads it from the frame, an int or a flag as a float; NaN for a name with no live row. */
    operator fun get(name: String): Float = reads.get(frame, name)

    fun int(name: String): Int = get(name).roundToInt()

    fun flag(name: String): Boolean = get(name) > 0.5f

    /** These params with [name] set to [value]; the saved palette's id kept. Throws for a name with no live row. */
    fun with(
        name: String,
        value: Float,
    ): SceneParams = SceneParams(checkNotNull(reads.set(frame, name, value)) { "no live scene param named $name" }, customPaletteId)

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

    /** The hue base of the look's palette: the override when set, else the built-in palette's (native). */
    val paletteBase: Float get() = ParamsNative.paramsPaletteBase(int("palette"), get("paletteBaseOverride"))

    val paletteRange: Float get() = ParamsNative.paramsPaletteRange(int("palette"), get("paletteRangeOverride"))

    /** The built-in palette again: overrides, stops and the saved palette's id gone. */
    fun withoutCustomPalette(): SceneParams = SceneParams(ParamsNative.paramsClearCustomPalette(frame), NO_CUSTOM_PALETTE)

    /** A hue gradient as the look's own palette, from saved palette [id] if any. */
    fun withGradient(
        base: Float,
        span: Float,
        id: String = NO_CUSTOM_PALETTE,
    ): SceneParams = SceneParams(ParamsNative.paramsApplyGradient(frame, base, span), id)

    /** Your [stops] (the first five) as the look's own palette, from saved palette [id] if any. */
    fun withStops(
        stops: List<Int>,
        id: String = NO_CUSTOM_PALETTE,
    ): SceneParams = SceneParams(ParamsNative.paramsApplyStops(frame, stops.toIntArray()), id)

    /**
     * These params (a look) as they replace the live [current]: the input preferences stay [current]'s (native's
     * kInput rows), the rest is this look's.
     */
    fun keepingInputsOf(current: SceneParams): SceneParams =
        SceneParams(ParamsNative.paramsKeepInputs(current.frame, frame), customPaletteId)

    /** The same frame naming saved palette [id]. */
    fun withPaletteId(id: String): SceneParams = SceneParams(frame, id)

    /** Saved palette [paletteText] (its file's text) as the look's own palette, named [id] (native's apply rule). */
    fun withSavedPalette(
        paletteText: String,
        id: String,
    ): SceneParams = SceneParams(LooksNative.savedPaletteApply(paletteText, frame), id)

    /** These params once saved palette [deletedId] is deleted: native says which id the look keeps. */
    fun forgetPalette(deletedId: String): SceneParams = withPaletteId(LooksNative.savedPaletteKeptId(customPaletteId, deletedId))

    override fun equals(other: Any?): Boolean =
        other is SceneParams && other.customPaletteId == customPaletteId && other.frame.contentEquals(frame)

    override fun hashCode(): Int = 31 * frame.contentHashCode() + customPaletteId.hashCode()

    override fun toString(): String = "SceneParams(${frame.size} slots, palette '$customPaletteId')"

    /** The frame's questions. The app asks native; a JVM test, which cannot load the library, installs its own. */
    interface Reads {
        fun defaults(): FloatArray

        fun get(
            frame: FloatArray,
            name: String,
        ): Float

        fun set(
            frame: FloatArray,
            name: String,
            value: Float,
        ): FloatArray?

        /** The saved palette id a look naming [lookId] keeps on [frame], a frame native wrote. */
        fun paletteIdOn(
            frame: FloatArray,
            lookId: String,
        ): String
    }

    private object Native : Reads {
        override fun defaults(): FloatArray = ParamsNative.paramsDefaults()

        override fun get(
            frame: FloatArray,
            name: String,
        ): Float = ParamsNative.paramsGet(frame, name)

        override fun set(
            frame: FloatArray,
            name: String,
            value: Float,
        ): FloatArray? = ParamsNative.paramsSet(frame, name, value)

        override fun paletteIdOn(
            frame: FloatArray,
            lookId: String,
        ): String = LooksNative.savedPaletteFrameId(lookId, frame)
    }

    companion object {
        const val NO_CUSTOM_PALETTE: String = ""

        @Volatile
        private var reads: Reads = Native

        @Volatile
        private var defaults: SceneParams? = null

        /** Asks [next] from now on and forgets the kept defaults. */
        fun install(next: Reads) {
            reads = next
            defaults = null
        }

        /** Every param at its native default; asked once and kept. */
        val DEFAULT: SceneParams
            get() = defaults ?: SceneParams(reads.defaults(), NO_CUSTOM_PALETTE).also { defaults = it }

        /** The frame's float count, native's. */
        val FIELDS: Int get() = DEFAULT.frame.size

        /**
         * A frame native wrote (a roll, a reset) as params. [base]'s saved palette id carries over only while the
         * frame still draws its own palette (native's custom-palette rule): a saved palette IS its overrides.
         */
        fun fromFrame(
            values: FloatArray,
            base: SceneParams = DEFAULT,
        ): SceneParams {
            val frame = values.copyOf(FIELDS)
            return SceneParams(frame, reads.paletteIdOn(frame, base.customPaletteId))
        }

        /** A frame and its saved palette id as native read them (a preset, a template's look). */
        fun read(
            values: FloatArray,
            customPaletteId: String,
        ): SceneParams = SceneParams(values.copyOf(FIELDS), customPaletteId)
    }
}
