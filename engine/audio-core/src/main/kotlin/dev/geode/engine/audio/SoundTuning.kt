package dev.geode.engine.audio

/**
 * The user's tuning of the sound engine (Settings › Audio), global: `GeodeSoundTuning` in
 * `core/sound/SoundFrame.h`. Three-element lists are low, middle, high (or kick, snare, hat for the
 * drum ones). The defaults are the engine's own (`SoundEngine::defaultTuning`).
 *
 * Pure Kotlin so the layout is testable off-device; [toFloats] is the wire form native copies
 * field for field, and `SoundTuningTest` checks it against the header.
 */
data class SoundTuning(
    /** One multiplier on every shaped band, 0..3. */
    val reactivity: Float = 1f,
    /** One multiplier on every clock, 0..4. */
    val speed: Float = 1f,
    val gain: List<Float> = listOf(1f, 1f, 1f),
    val threshold: List<Float> = listOf(0f, 0f, 0f),
    val offset: List<Float> = listOf(0f, 0f, 0f),
    val attack: List<Float> = listOf(0.03f, 0.03f, 0.03f),
    val release: List<Float> = listOf(0.15f, 0.15f, 0.15f),
    /** How hard a kick, snare or hat must hit to count, 0.1..3. */
    val drum: List<Float> = listOf(0.6f, 0.5f, 0.5f),
    val drumDecay: List<Float> = listOf(0.18f, 0.15f, 0.08f),
    val onsetThreshold: Float = 2f,
    val hardCutLevel: Float = 2.5f,
    val hardCutHalfLife: Float = 60f,
    val pitchThreshold: Float = 0.35f,
    val noteHold: Float = 0.08f,
    val silenceDb: Float = -60f,
) {
    /** Values outside what the engine accepts, clamped to it; lists that are not three long reset. */
    fun clamped(): SoundTuning {
        val d = SoundTuning()

        fun List<Float>.fit(
            def: List<Float>,
            range: ClosedFloatingPointRange<Float>,
        ) = if (size == 3) map { if (it.isFinite()) it.coerceIn(range) else range.start } else def
        return SoundTuning(
            reactivity = reactivity.fin(d.reactivity).coerceIn(REACTIVITY),
            speed = speed.fin(d.speed).coerceIn(SPEED),
            gain = gain.fit(d.gain, GAIN),
            threshold = threshold.fit(d.threshold, 0f..1f),
            offset = offset.fit(d.offset, -1f..1f),
            attack = attack.fit(d.attack, 0f..1f),
            release = release.fit(d.release, 0f..2f),
            drum = drum.fit(d.drum, DRUM),
            drumDecay = drumDecay.fit(d.drumDecay, 0.02f..1f),
            onsetThreshold = onsetThreshold.fin(d.onsetThreshold).coerceIn(1f..4f),
            hardCutLevel = hardCutLevel.fin(d.hardCutLevel).coerceIn(1.5f..4f),
            hardCutHalfLife = hardCutHalfLife.fin(d.hardCutHalfLife).coerceIn(5f..180f),
            pitchThreshold = pitchThreshold.fin(d.pitchThreshold).coerceIn(0.1f..0.9f),
            noteHold = noteHold.fin(d.noteHold).coerceIn(0.02f..0.5f),
            silenceDb = silenceDb.fin(d.silenceDb).coerceIn(-90f..-30f),
        )
    }

    /** `GeodeSoundTuning` field order, [SoundFrameLayout.TUNING_FLOATS] long. */
    fun toFloats(): FloatArray {
        val c = clamped()
        return (
            c.gain + c.threshold + c.offset + c.attack + c.release + c.drum + c.drumDecay +
                listOf(
                    c.reactivity,
                    c.speed,
                    c.onsetThreshold,
                    c.hardCutLevel,
                    c.hardCutHalfLife,
                    c.pitchThreshold,
                    c.noteHold,
                    c.silenceDb,
                )
        ).toFloatArray()
    }

    companion object {
        val REACTIVITY = 0f..3f
        val SPEED = 0f..4f
        val GAIN = 0f..4f
        val DRUM = 0.1f..3f

        private fun Float.fin(default: Float) = if (isFinite()) this else default

        /** The inverse of [toFloats]; anything the wrong length is the defaults. */
        fun fromFloats(f: FloatArray): SoundTuning {
            if (f.size != SoundFrameLayout.TUNING_FLOATS) return SoundTuning()
            fun three(at: Int) = listOf(f[at], f[at + 1], f[at + 2])
            return SoundTuning(
                gain = three(0),
                threshold = three(3),
                offset = three(6),
                attack = three(9),
                release = three(12),
                drum = three(15),
                drumDecay = three(18),
                reactivity = f[21],
                speed = f[22],
                onsetThreshold = f[23],
                hardCutLevel = f[24],
                hardCutHalfLife = f[25],
                pitchThreshold = f[26],
                noteHold = f[27],
                silenceDb = f[28],
            ).clamped()
        }
    }
}
