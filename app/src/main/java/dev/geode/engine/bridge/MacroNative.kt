package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for the macro bank (CLAUDE.md).
object MacroNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * The macro bank (core/viz/MacroBank), as the text the store keeps: [macroBankSet] makes it the one
     * every renderer in the process adds to its params; the others return a new bank text and decide
     * everything (unreadable text is the default bank, a refused edit the bank unchanged).
     */
    external fun macroBankSet(bank: String)

    external fun macroBankDefault(): String

    external fun macroBankNormalize(bank: String): String

    external fun macroBankSetValue(
        bank: String,
        knobId: Int,
        value: Float,
    ): String

    external fun macroBankRename(
        bank: String,
        knobId: Int,
        name: String,
    ): String

    external fun macroBankAddKnob(
        bank: String,
        name: String,
    ): String

    /** Refused for a built-in knob, which [macroBankResetKnob] puts back as built instead. */
    external fun macroBankRemoveKnob(
        bank: String,
        knobId: Int,
    ): String

    external fun macroBankDuplicateKnob(
        bank: String,
        knobId: Int,
    ): String

    external fun macroBankResetKnob(
        bank: String,
        knobId: Int,
    ): String

    /** The knob [delta] places along the bank, negative earlier. */
    external fun macroBankMoveKnob(
        bank: String,
        knobId: Int,
        delta: Int,
    ): String

    /** [target] is a modulation target's wire name; [up] and [down] are -1..1 of its range at a full turn each way. */
    external fun macroBankSetLink(
        bank: String,
        knobId: Int,
        target: String,
        up: Float,
        down: Float,
    ): String

    /** Links a target the knob does not move yet, at the weights native starts a link on. */
    external fun macroBankAddLink(
        bank: String,
        knobId: Int,
        target: String,
    ): String

    external fun macroBankRemoveLink(
        bank: String,
        knobId: Int,
        target: String,
    ): String

    /** The knob editor's one read on [styleId]: the caps, each knob with what it offers, its links, every target. */
    external fun macroBankEditor(
        bank: String,
        styleId: String,
    ): String

    /** How a link follows the knob: [curve] is LINEAR, EASE_IN, EASE_OUT or S_CURVE. */
    external fun macroBankSetCurve(
        bank: String,
        knobId: Int,
        target: String,
        curve: String,
    ): String

    /** A new knob that moves the Customize control [control] (a control key); native names it. */
    external fun macroBankAddKnobFor(
        bank: String,
        control: String,
    ): String

    /** Every Customize control a knob can move, its category, and the knobs that move it now. */
    external fun macroBankControls(bank: String): String

    /**
     * What the waves and envelopes on a knob add to its turn on the on-screen surface's last frame, one
     * `mod|<knob id>|<add>|<sources: 1 wave, 2 envelope, 3 both>` row per knob; the knob's second arc polls it.
     */
    external fun macroBankLiveMods(): String

    /** New ranges and curves for the links the knob has, their params kept; the same [seed], the same ranges. */
    external fun macroBankRollRanges(
        bank: String,
        knobId: Int,
        seed: Long,
    ): String

    /** Up to [count] more params on the knob, picked at random among those [styleId] reaches and Customize does not keep. */
    external fun macroBankAddRandom(
        bank: String,
        knobId: Int,
        styleId: String,
        count: Int,
        seed: Long,
    ): String

    /** A new turn for every knob on screen that moves something on [styleId]; the same [seed], the same turns. */
    external fun macroBankRollValues(
        bank: String,
        styleId: String,
        seed: Long,
    ): String

    /** The knob off screen into the bank's library, centred. */
    external fun macroBankHideKnob(
        bank: String,
        knobId: Int,
    ): String

    /** A knob from the library back on screen, last; refused when the screen is full. */
    external fun macroBankShowKnob(
        bank: String,
        knobId: Int,
    ): String

    /** The name native offers when the knob is saved (a name, not a bank). */
    external fun macroBankAutoName(
        bank: String,
        knobId: Int,
    ): String
}
