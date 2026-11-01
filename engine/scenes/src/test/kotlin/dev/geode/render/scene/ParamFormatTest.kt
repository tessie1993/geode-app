package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Test

/** The unit beside each Customize value. */
class ParamFormatTest {
    @Test
    fun amountsArePercentAndSignedWhenBipolar() {
        assertEquals("35 %", ParamFormat.format(0.35f, ParamUnit.PERCENT))
        assertEquals("+35 %", ParamFormat.format(0.35f, ParamUnit.PERCENT, bipolar = true))
        assertEquals("−20 %", ParamFormat.format(-0.2f, ParamUnit.PERCENT, bipolar = true))
        assertEquals("0 %", ParamFormat.format(0f, ParamUnit.PERCENT, bipolar = true))
    }

    @Test
    fun multipliersCarryTimes() {
        assertEquals("1.8×", ParamFormat.format(1.8f, ParamUnit.TIMES))
        assertEquals("0.25×", ParamFormat.format(0.25f, ParamUnit.TIMES))
        assertEquals("2×", ParamFormat.format(2f, ParamUnit.TIMES))
        assertEquals("−1.5×", ParamFormat.format(-1.5f, ParamUnit.TIMES))
    }

    @Test
    fun timesSwitchToMillisecondsUnderASecond() {
        assertEquals("2.5 s", ParamFormat.format(2.5f, ParamUnit.SECONDS))
        assertEquals("35 ms", ParamFormat.format(0.035f, ParamUnit.SECONDS))
        assertEquals("1.0 s", ParamFormat.format(1f, ParamUnit.SECONDS))
    }

    @Test
    fun frequenciesAnglesTemposAndCounts() {
        assertEquals("110 Hz", ParamFormat.format(110f, ParamUnit.HERTZ))
        assertEquals("0.25 Hz", ParamFormat.format(0.25f, ParamUnit.HERTZ))
        assertEquals("2.3 kHz", ParamFormat.format(2300f, ParamUnit.HERTZ))
        assertEquals("90°", ParamFormat.format(0.25f, ParamUnit.TURN_DEGREES))
        assertEquals("124 BPM", ParamFormat.format(124.2f, ParamUnit.BPM))
        assertEquals("6", ParamFormat.format(6f, ParamUnit.COUNT))
        assertEquals("12.5", ParamFormat.format(12.5f, ParamUnit.PLAIN))
    }

    @Test
    fun theDefaultSaysDefault() {
        assertEquals(ParamFormat.DEFAULT_TEXT, ParamFormat.format(1f, ParamUnit.TIMES, default = 1f))
        assertEquals("1.2×", ParamFormat.format(1.2f, ParamUnit.TIMES, default = 1f))
    }

    @Test
    fun unitsComeFromTheTableThenTheRange() {
        assertEquals(ParamUnit.TIMES, ParamFormat.unitFor(ParamKeys.SPEED, 0.05f..4f))
        assertEquals(ParamUnit.TURN_DEGREES, ParamFormat.unitFor(ParamKeys.HUE_SHIFT, 0f..1f))
        assertEquals(ParamUnit.HERTZ, ParamFormat.unitFor(ParamKeys.FUNDAMENTAL_HZ, 40f..440f))
        assertEquals(ParamUnit.PERCENT, ParamFormat.unitFor(ParamKeys.VIGNETTE, 0f..1f))
        assertEquals(ParamUnit.PERCENT, ParamFormat.unitFor(ParamKeys.TWIST, -1f..1f))
        assertEquals(ParamUnit.TIMES, ParamFormat.unitFor("Unknown gain", 0f..2f))
        assertEquals(ParamUnit.PLAIN, ParamFormat.unitFor("Unknown big", 0f..50f))
        assertEquals("−50 %", ParamFormat.formatParam(ParamKeys.TWIST, -0.5f, -1f..1f, default = 0f))
    }
}
