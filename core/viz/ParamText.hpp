#pragma once

#include <string>
#include <string_view>

// The text a Customize control shows for its value, and which unit that control reads in. Pure; host-tested
// in core/tests/param_text_test.cpp.
//
// A bare number means nothing on its own: 0.35 is a 35 % mix on one row, a 0.35 s fall on the next and a
// third of a turn of hue on a third. The unit makes each one readable.
namespace geode::viz {

// The order is the wire's (geode_param_text's `unit`).
enum class ParamUnit : int {
    Percent = 0,   // an amount, depth or mix: 0..1 as 0-100 %, signed when the range goes below zero
    Times,         // a gain, multiplier or speed: 1.8×
    Seconds,       // 2.5 s, or 35 ms under one second
    Hertz,         // 110 Hz, 2.3 kHz
    TurnDegrees,   // a fraction of a turn (hue shift): 90°
    Plain,         // no natural unit and a range past 0..1 (fluid curl)
};

// The unit the control named `key` reads in: its own where it has one, else by its range lo..hi (inside
// -1..1 a percentage, inside 0..2 a multiplier, anything wider plain).
ParamUnit paramUnitFor(std::string_view key, float lo, float hi);

// `value` in `unit`. At `def` (NaN for a control with no default) it reads "Default". `bipolar` signs a
// percentage (+35 %, −20 %). Negative numbers carry a true minus sign (U+2212).
std::string paramText(float value, ParamUnit unit, float def, bool bipolar);

}  // namespace geode::viz
