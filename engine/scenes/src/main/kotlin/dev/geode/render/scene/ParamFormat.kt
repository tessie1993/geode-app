package dev.geode.render.scene

import java.util.Locale
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * What kind of number a Customize control holds, which decides the unit printed after it.
 *
 * WHY: the panel used to print every value as a bare `"%.2f"`, so "0.35" meant a 35 % mix on one
 * row, a 0.35 s fall on the next and a third of a turn of hue on a third. A listener cannot read
 * that; a unit makes each number mean something.
 */
enum class ParamUnit {
    /** An amount, depth or mix: 0..1 shown as 0–100 %, signed when the range is bipolar. */
    PERCENT,

    /** A gain, multiplier or speed: `1.8×`. */
    TIMES,

    /** A time in seconds: `2.5 s`, or `35 ms` under one second. */
    SECONDS,

    /** A frequency: `110 Hz`, `2.3 kHz`. */
    HERTZ,

    /** A fraction of a turn (hue shift): `90°`. */
    TURN_DEGREES,

    /** A tempo: `124 BPM`. Reserved for the tempo controls of a later change. */
    BPM,

    /** A whole count: symmetry folds, spawn points. */
    COUNT,

    /** A number with no natural unit and a range past 0..1 (fluid curl). */
    PLAIN,
}

/**
 * Turns a control's value into the words shown beside it. Pure, so it is tested on the JVM.
 *
 * "Default" stands in for the number when the control sits at its default: what a listener wants
 * to know at a glance is whether it has been touched, and a reset button appears beside it when it has.
 */
object ParamFormat {
    const val DEFAULT_TEXT = "Default"

    private const val DEFAULT_EPSILON = 1e-4f
    private const val PERCENT = 100f
    private const val DEGREES_PER_TURN = 360f
    private const val MS_PER_S = 1000f
    private const val HZ_PER_KHZ = 1000f
    private const val TWO_PLACES_BELOW = 10f

    // U+2212, so a negative value lines up with a positive one instead of showing a short hyphen.
    private const val MINUS = "−"

    /** Controls whose unit the range alone would get wrong. Keyed by [ParamKeys] label. */
    private val UNITS: Map<String, ParamUnit> =
        mapOf(
            ParamKeys.SPEED to ParamUnit.TIMES,
            ParamKeys.ZOOM to ParamUnit.TIMES,
            ParamKeys.ROTATION to ParamUnit.TIMES,
            ParamKeys.AUDIO_DRIVE to ParamUnit.TIMES,
            ParamKeys.BASS_GAIN to ParamUnit.TIMES,
            ParamKeys.MID_GAIN to ParamUnit.TIMES,
            ParamKeys.TREBLE_GAIN to ParamUnit.TIMES,
            ParamKeys.PARTICLE_SIZE to ParamUnit.TIMES,
            ParamKeys.TILE to ParamUnit.TIMES,
            ParamKeys.MARCH_DETAIL to ParamUnit.TIMES,
            ParamKeys.CYCLE_SPEED to ParamUnit.TIMES,
            ParamKeys.CAM_FLY to ParamUnit.TIMES,
            ParamKeys.CAM_FOV to ParamUnit.TIMES,
            ParamKeys.REACT_AMOUNT to ParamUnit.TIMES,
            ParamKeys.FIELD_SCALE to ParamUnit.TIMES,
            ParamKeys.STIRRER_SPEED to ParamUnit.TIMES,
            ParamKeys.HUE_SHIFT to ParamUnit.TURN_DEGREES,
            ParamKeys.FUNDAMENTAL_HZ to ParamUnit.HERTZ,
            ParamKeys.PARTICLE_LIFE_S to ParamUnit.SECONDS,
            ParamKeys.FLUID_CURL to ParamUnit.PLAIN,
            ParamKeys.FLOW_CURL to ParamUnit.PLAIN,
        )

    /** The unit for [key], from the table above or else from its [range]. */
    fun unitFor(
        key: String,
        range: ClosedFloatingPointRange<Float>,
    ): ParamUnit =
        UNITS[key] ?: when {
            range.start >= -1f && range.endInclusive <= 1f -> ParamUnit.PERCENT
            range.start >= 0f && range.endInclusive <= 2f -> ParamUnit.TIMES
            else -> ParamUnit.PLAIN
        }

    /** [value] in [unit]; "Default" when it equals [default]. */
    fun format(
        value: Float,
        unit: ParamUnit,
        default: Float? = null,
        bipolar: Boolean = false,
    ): String {
        if (default != null && abs(value - default) < DEFAULT_EPSILON) return DEFAULT_TEXT
        return when (unit) {
            ParamUnit.PERCENT -> {
                val pct = (value * PERCENT).roundToInt()
                when {
                    !bipolar -> "$pct %"
                    pct > 0 -> "+$pct %"
                    pct < 0 -> "$MINUS${-pct} %"
                    else -> "0 %"
                }
            }
            ParamUnit.TIMES -> "${signed(trim(value))}×"
            ParamUnit.SECONDS ->
                if (abs(value) >= 1f) {
                    "${signed(oneDecimal(value))} s"
                } else {
                    "${signed((value * MS_PER_S).roundToInt().toString())} ms"
                }
            ParamUnit.HERTZ ->
                when {
                    value >= HZ_PER_KHZ -> "${oneDecimal(value / HZ_PER_KHZ)} kHz"
                    value >= TWO_PLACES_BELOW -> "${value.roundToInt()} Hz"
                    else -> "${trim(value)} Hz"
                }
            ParamUnit.TURN_DEGREES -> "${signed((value * DEGREES_PER_TURN).roundToInt().toString())}°"
            ParamUnit.BPM -> "${value.roundToInt()} BPM"
            ParamUnit.COUNT -> signed(value.roundToInt().toString())
            ParamUnit.PLAIN -> signed(trim(value))
        }
    }

    /** [value] of the control [key] with [range]: the one call the sliders make. */
    fun formatParam(
        key: String,
        value: Float,
        range: ClosedFloatingPointRange<Float>,
        default: Float? = null,
    ): String = format(value, unitFor(key, range), default, bipolar = range.start < 0f)

    /** Up to two decimals, trailing zeros dropped: 1.8, 0.25, 2. */
    private fun trim(value: Float): String =
        String
            .format(Locale.ROOT, "%.2f", value)
            .trimEnd('0')
            .trimEnd('.')
            .let { if (it == "-0") "0" else it }

    private fun oneDecimal(value: Float): String = String.format(Locale.ROOT, "%.1f", value)

    private fun signed(text: String): String = if (text.startsWith("-")) MINUS + text.drop(1) else text
}
