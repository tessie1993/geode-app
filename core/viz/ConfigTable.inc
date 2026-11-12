// The config params schema (#423): every value the data stores hold beside the scene params, written
// once. The scene params are viz/ParamTable.inc; these are the modulation slots, the palette maker,
// the look's envelope and the preset morph. Not a header: it holds
// only GEODE_CONFIG rows, and each includer defines GEODE_CONFIG, includes this file and #undefs it.
//
//   GEODE_CONFIG(group, name, kind, default, min, max, unit)
//
//   group    which store or engine owns the value: lfo, adsr, palette, look, clip, theme.
//   name     the store's JSON or prefs key where it has one, else the Kotlin property. The slot is
//            config::Slot::<group>_<name>.
//   kind     Float, Int (an enum ordinal or a count) or Bool.
//   default  what a fresh slot or a missing key reads as.
//   min, max the control range: what the store clamps to and the slider offers. Ints and Bools count
//            choices: a Bool is 0..1, an enum its first..last ordinal.
//   unit     None (a plain number, a flag or an enum), Seconds, Fraction (0..1, shown as a percent),
//            Turns (a fraction of a hue turn), Degrees, Points (percentage points), Times (a multiplier).
//
// Inside a group the row order is the order the engine takes the values: the lfo rows are the
// floats of a slot's wire (waves::toWire), the adsr rows the envelope's before its target list.
// Append within a group; never reorder.
// core/tests/config_schema_test.cpp checks the table; geode_config_schema_text publishes it.
//
// Where native already enforced a number, the row takes native's; where only Kotlin had one, the row
// copies it and names the file and line. A few engine clamps are not control ranges (the LFO's depth
// ceiling with chained modulation, the envelope's shortest stage on decay and release): they stay in
// the engine, named, and the comment at each says why.

// ---- LFO slots (viz/Lfo, LfoStore.kt) --------------------------------------------------------------
GEODE_CONFIG(lfo, enabled, Bool, 0, 0, 1, None)
// ModSource: Lfo .. StereoPan.
GEODE_CONFIG(lfo, source, Int, 0, 0, 8, None)
// LfoTarget: None .. PaletteShift (kLastLfoTarget; ordinal 25 is the retired Glitch).
GEODE_CONFIG(lfo, target, Int, 0, 0, 67, None)
// LfoWave: Sine .. RandomGlide.
GEODE_CONFIG(lfo, wave, Int, 0, 0, 5, None)
// The period, not a frequency (LfoConfig.kt and the old Lfo.hpp kDefault/kMin/kMaxRateSeconds: 2, 0.05,
// 60). A slot saved in Hz migrates through config::lfoRateSecondsFromHz.
GEODE_CONFIG(lfo, rateSeconds, Float, 2.0f, 0.05f, 60.0f, Seconds)
// Default 0.3 (Lfo.hpp, LfoStore.kt:131). The control range is Customize's slider (CustomizeAdvanced.kt:560);
// the engine lets a chained slot push depth up to LfoEngine::kMaxModulatedDepth.
GEODE_CONFIG(lfo, depth, Float, 0.3f, 0.0f, 1.0f, Fraction)
// ModPolarity: Bipolar, Positive, Negative.
GEODE_CONFIG(lfo, polarity, Int, 0, 0, 2, None)
// ModCurve: Linear, Exponential, Logarithmic, Smooth.
GEODE_CONFIG(lfo, curve, Int, 0, 0, 3, None)
// Beat timing (Lfo.hpp LfoConfig::beats): 0 is free (rateSeconds), else 1, 2, 4, 8, 16 or 32 beats a cycle;
// any other value reads as free.
GEODE_CONFIG(lfo, beats, Float, 0.0f, 0.0f, 32.0f, None)
// The macro knob a wave on LfoTarget::Macro turns, by its id (viz/MacroBank); 0 is none.
GEODE_CONFIG(lfo, knob, Int, 0, 0, 999999, None)
// On a moment (viz/Reactions Moment: Kick .. Loudness): the wave restarts its cycle on it; -1 runs free.
GEODE_CONFIG(lfo, moment, Int, -1, -1, 9, None)

// ---- Envelopes (viz/Adsr, LfoStore.kt adsrFromJson) -----------------------------------------------
// Defaults are Adsr.hpp's (and Adsr.kt's, LfoStore.kt:102-108); ranges are Customize's sliders
// (CustomizeAdvanced.kt:656-669). The engine's shortest stage is attack's min, for every stage.
GEODE_CONFIG(adsr, enabled, Bool, 0, 0, 1, None)
GEODE_CONFIG(adsr, attack, Float, 0.05f, 0.005f, 1.0f, Seconds)
GEODE_CONFIG(adsr, decay, Float, 0.25f, 0.01f, 1.5f, Seconds)
GEODE_CONFIG(adsr, sustain, Float, 0.5f, 0.0f, 1.0f, Fraction)
GEODE_CONFIG(adsr, release, Float, 0.35f, 0.02f, 2.0f, Seconds)
GEODE_CONFIG(adsr, amount, Float, 0.5f, 0.0f, 1.5f, Fraction)
// EnvBand: Bass, Mid, Treble, Rms, Brightness, Width.
GEODE_CONFIG(adsr, band, Int, 0, 0, 5, None)
// The min is also the engine's floor when sustain tracks the level (Adsr.cpp divides by it).
GEODE_CONFIG(adsr, gateThreshold, Float, 0.25f, 0.05f, 1.0f, Fraction)
GEODE_CONFIG(adsr, sustainTrack, Bool, 0, 0, 1, None)
GEODE_CONFIG(adsr, retrigger, Bool, 1, 0, 1, None)
// The macro knob the envelope turns when LfoTarget::Macro is among its targets, by id; 0 is none.
GEODE_CONFIG(adsr, knob, Int, 0, 0, 999999, None)
// On a moment (viz/Reactions Moment): the envelope fires on it instead of on a note start; -1 is a note start.
GEODE_CONFIG(adsr, moment, Int, -1, -1, 9, None)

// ---- Palette maker's gradient (PaletteStore.kt) ----------------------------------------------------
// What applyGradient writes into the four palette overrides (PaletteStore.kt:77-78) and a saved palette
// reads as (:150-151, :168-169, defaults 0 and 1). The scene param rows run from -1 because -1 is
// their "no override" sentinel; a set gradient is always 0..1.
GEODE_CONFIG(palette, baseHue, Float, 0.0f, 0.0f, 1.0f, Turns)
GEODE_CONFIG(palette, hueSpan, Float, 1.0f, 0.0f, 1.0f, Turns)

// ---- The look's envelope (PresetStore.kt, TemplateFormat.kt) -----------------------------------------
// Defaults TemplateLook.DEFAULT_ATTACK and DEFAULT_DECAY (VideoTemplate.kt:181-182); ranges
// PresetStore.ATTACK_RANGE and DECAY_RANGE (PresetStore.kt:347-348), which TemplateFormat.readLook
// also clamps to (TemplateFormat.kt:328-329).
GEODE_CONFIG(look, attack, Float, 0.6f, 0.005f, 1.0f, Seconds)
GEODE_CONFIG(look, decay, Float, 0.12f, 0.01f, 1.5f, Seconds)

// ---- Preset morph (ThemeStore.kt) ------------------------------------------------------------------
// GuiPrefs.presetMorphSeconds: default 2, at most PRESET_MORPH_SECONDS_MAX. Seconds, never beats.
GEODE_CONFIG(theme, presetMorphSeconds, Float, 2.0f, 0.0f, 8.0f, Seconds)
