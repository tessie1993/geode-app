package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

/**
 * The user's tuning of the sound engine (Settings › Audio), global: `GeodeSoundTuning` in
 * `core/sound/SoundFrame.h` as its [SoundFrameLayout.TUNING_FLOATS] floats, one per slot in field order
 * (the slot constants below). Three-slot groups are low, middle, high (or kick, snare, hat for the drums).
 *
 * Only a holder. Every default, range and the clamp (NaN and infinity become the default) are native,
 * `core/sound/Tuning`, so the engine, these sliders and the saved settings share one schema. [SoundTuning]
 * with no saved floats means "the engine's defaults" and stays unresolved until a value is read: storage
 * ([savedFloats], [fromFloats]) and equality never load the native library, so JVM tests of the prefs
 * can still build one.
 */
class SoundTuning private constructor(
    private val saved: FloatArray?,
) {
    constructor() : this(null)

    /** The saved floats clamped by native, or its defaults; resolved on first read, on a thread that has the library. */
    private val values: FloatArray by lazy {
        saved?.copyOf()?.also(GeodeNative::soundTuningClamp) ?: Schema.floats.copyOf(SoundFrameLayout.TUNING_FLOATS)
    }

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
        const val GAIN = 0
        const val THRESHOLD = 3
        const val OFFSET = 6
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

        /** The slider range of [slot], native's. */
        fun range(slot: Int): ClosedFloatingPointRange<Float> {
            val n = SoundFrameLayout.TUNING_FLOATS
            return Schema.floats[n + slot]..Schema.floats[2 * n + slot]
        }

        /** Saved floats, clamped when first read; anything the wrong length is the defaults. */
        fun fromFloats(f: FloatArray) = if (f.size == SoundFrameLayout.TUNING_FLOATS) SoundTuning(f.copyOf()) else SoundTuning()
    }
}
