package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for style parameters and slider maths (CLAUDE.md).
object ParamsNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** Every scene param at its default, as a new `geode_viz_set_params` frame (`geode_params_defaults`, core/viz/Params). */
    external fun paramsDefaults(): FloatArray

    /** Param [name] of the params [frame], an int or a flag as a float; NaN for a name with no live row (`geode_params_get`). */
    external fun paramsGet(
        frame: FloatArray,
        name: String,
    ): Float

    /** [frame] with param [name] at [value], as a new frame; null for a name with no live row (`geode_params_set`). */
    external fun paramsSet(
        frame: FloatArray,
        name: String,
        value: Float,
    ): FloatArray?

    /**
     * The text a Customize control shows for [value] (core/viz/ParamText, `geode_param_text`): a %, ×, s or
     * ms, Hz or °, and "Default" at [default] (NaN for a control with none). [unit] is one of [PARAM_UNIT_PERCENT] and the rest;
     * -1 takes the control's own, by its [key] and then its range [lo]..[hi]. A range below zero signs a
     * percentage.
     */
    external fun paramText(
        key: String,
        unit: Int,
        value: Float,
        lo: Float,
        hi: Float,
        default: Float,
    ): String

    /** The units [paramText] takes (`geode_param_text`); [PARAM_UNIT_OWN] takes the control's own. */
    const val PARAM_UNIT_OWN: Int = -1
    const val PARAM_UNIT_PERCENT: Int = 0
    const val PARAM_UNIT_TIMES: Int = 1
    const val PARAM_UNIT_SECONDS: Int = 2

    /**
     * The hue base a palette slot draws: [override] when it is >= 0, else built-in palette [palette]'s, the
     * index clamped into the table (`geode_params_palette_base`, core/viz/Params). Pure.
     */
    external fun paramsPaletteBase(
        palette: Int,
        override: Float,
    ): Float

    /** [paramsPaletteBase] for the hue span (`geode_params_palette_range`). */
    external fun paramsPaletteRange(
        palette: Int,
        override: Float,
    ): Float

    /** True when the params [frame] draws its own palette, a hue override or a first stop set (`geode_params_uses_custom_palette`). */
    external fun paramsUsesCustomPalette(frame: FloatArray): Boolean

    /** Your colours' stops set from the first in the params [frame] (`geode_params_palette_stops`). */
    external fun paramsPaletteStops(frame: FloatArray): IntArray

    /** [frame] with both hue overrides and every stop unset, as a new frame (`geode_params_clear_custom_palette`). */
    external fun paramsClearCustomPalette(frame: FloatArray): FloatArray

    /** [frame] cleared, then the first five of [stops] as your colours (`geode_params_apply_stops`). */
    external fun paramsApplyStops(
        frame: FloatArray,
        stops: IntArray,
    ): FloatArray

    /** [frame] with no stops and the hue gradient [base], [span] (held to 0..1) as its overrides (`geode_params_apply_gradient`). */
    external fun paramsApplyGradient(
        frame: FloatArray,
        base: Float,
        span: Float,
    ): FloatArray

    /**
     * The look frame [look] as it replaces the live frame [current]: the input preferences (Tilt phone to look, Drag
     * to look) stay [current]'s, every other slot is [look]'s, as a new frame (`geode_params_keep_inputs`). Pure.
     */
    external fun paramsKeepInputs(
        current: FloatArray,
        look: FloatArray,
    ): FloatArray

    /**
     * The scene params object of a preset, link, template look or saved live look (core/viz/ParamsJson): every live
     * row of the wire [frame] under its name as org.json wrote it, then the two palette slot ids, as compact JSON
     * (`geode_params_write_json`). Null for a short frame. Pure.
     */
    external fun paramsWriteJson(
        frame: FloatArray,
        customPaletteId: String,
        customPalette2Id: String,
    ): String?

    /**
     * A params object read as org.json's opt* read it, sanitized into [out] (a wire frame), and the two palette slot
     * ids into [ids] (two slots; "" when missing) (`geode_params_read_json`): the object at [path] (nested keys,
     * newline-separated, "" for the top) of [document], the text org.json parsed, when that is standard JSON, else
     * [fallback], the object as org.json printed it. False, nothing written, when neither holds an object. Pure.
     */
    external fun paramsReadJson(
        document: String?,
        path: String,
        fallback: String?,
        out: FloatArray,
        ids: Array<String?>,
    ): Boolean

    /** [samples] colours of what a params frame's own palette paints, its stops or its hue ramp (`geode_params_palette_preview`). Pure. */
    external fun paramsPalettePreview(
        params: FloatArray,
        samples: Int,
    ): IntArray

    /**
     * Randomize everything (core/viz/RandomizeAll), the two-circle gesture: a style among every catalogue
     * style not in [lockedStyles] (newline-joined ids), not [current] while another is open, MilkDrop
     * only when [milkCount] > 0. [picks] gets the index into [lookStyles] (each preset's style id, one
     * per line) of a preset made for it, or -1, then on MilkDrop with no preset a .milk index, or -1.
     * Null when no style is open. Pass the same [seed] to [PanelNative.panelRollAll].
     */
    external fun randomizeAllPick(
        seed: Long,
        current: String,
        lockedStyles: String,
        lookStyles: String,
        milkCount: Int,
        picks: IntArray,
    ): String?

    // A Customize slider's maths (core/viz/SliderMap, `geode_slider_*`): pure, cheap, called per drag event.

    /** True when Reset would move [value] to [default] (NaN for a control with none): more than 1e-4 off it. */
    external fun sliderOffDefault(
        value: Float,
        default: Float,
    ): Boolean

    /** The ticks between [first] and [last] of a whole-number slider. */
    external fun sliderIntSteps(
        first: Int,
        last: Int,
    ): Int

    /** The whole number a stepped slider's [position] means: rounded, clamped into [first]..[last]. */
    external fun sliderIntValue(
        position: Float,
        first: Int,
        last: Int,
    ): Int

    /** [value]'s 0..1 place on a log track over [lo]..[hi]. */
    external fun sliderLogPosition(
        value: Float,
        lo: Float,
        hi: Float,
    ): Float

    /** The value at 0..1 [position] on a log track over [lo]..[hi]: lo * (hi / lo)^position. */
    external fun sliderLogValue(
        position: Float,
        lo: Float,
        hi: Float,
    ): Float

    /**
     * The range Customize's slider offers for the scene param [name] (core/viz/ControlRange): min and max into
     * [out] (two floats). False, [out] untouched, for a name the schema lacks.
     */
    external fun paramsControlRange(
        name: String,
        out: FloatArray,
    ): Boolean
}
