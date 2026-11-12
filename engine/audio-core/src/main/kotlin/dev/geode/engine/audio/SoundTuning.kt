package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

/**
 * The user's tuning of the sound engine (Settings › Audio): `GeodeSoundTuning` (`core/sound/SoundFrame.h`) as
 * one float per slot in its field order; three-slot groups are low, middle, high (or kick, snare, hat). Only a
 * holder: defaults, ranges, the clamp and how an older save reads are native (`core/sound/Tuning`).
 * Saved floats stay as stored until read, and with none it is "the engine's defaults", so storage and
 * equality never load the native library and JVM tests of the prefs can build one.
 */
class SoundTuning private constructor(
    private val saved: FloatArray?,
) {
    constructor() : this(null)

    /** The saved floats as native reads them, or its defaults; resolved on first read, on a thread that has the library. */
    private val values: FloatArray by lazy { checkNotNull(GeodeNative.soundTuningFromSaved(saved)) }

    operator fun get(slot: Int): Float = values[slot]

    /** This tuning with one slot changed; [clamped] puts it back in range. */
    fun withSlot(
        slot: Int,
        value: Float,
    ) = SoundTuning(values.copyOf().also { it[slot] = value })

    fun clamped() = SoundTuning(values.copyOf())

    /** The wire form native copies field for field, clamped. */
    fun toFloats(): FloatArray = values.copyOf()

    /** What to save: null for the engine's defaults, so a reset follows the engine when they change. */
    fun savedFloats(): FloatArray? = saved?.copyOf()

    override fun equals(other: Any?) = other is SoundTuning && saved.contentEquals(other.saved)

    override fun hashCode() = saved.contentHashCode()

    private object Schema {
        val floats = checkNotNull(GeodeNative.soundTuningSchema())
    }

    companion object {
        // The slots Settings › Audio names, in `GeodeSoundTuning`'s field order (SoundTuningTest holds them to the header).
        const val GAIN = 0
        const val THRESHOLD = 3
        const val ATTACK = 9
        const val RELEASE = 12
        const val DRUM = 15
        const val DRUM_DECAY = 18
        const val REACTIVITY = 21
        const val SPEED = 22
        const val ONSET_THRESHOLD = 23
        const val HARD_CUT_LEVEL = 24
        const val HARD_CUT_HALF_LIFE = 25
        const val PITCH_THRESHOLD = 26
        const val NOTE_HOLD = 27
        const val SILENCE_DB = 28
        const val INPUT_TRIM_DB = 29

        /** The schema's rows: defaults, minimums, maximums. */
        private const val SCHEMA_ROWS = 3

        /** The slider range of [slot], native's. */
        fun range(slot: Int): ClosedFloatingPointRange<Float> {
            val n = Schema.floats.size / SCHEMA_ROWS
            return Schema.floats[n + slot]..Schema.floats[2 * n + slot]
        }

        /** Saved floats as stored; native reads them on first use (`geode_sound_tuning_from_saved`). */
        fun fromFloats(f: FloatArray): SoundTuning = SoundTuning(f.copyOf())
    }
}
