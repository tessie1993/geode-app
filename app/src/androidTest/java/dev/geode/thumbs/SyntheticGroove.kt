package dev.geode.thumbs

import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.sin

/**
 * A fixed four-bar loop at 120 BPM, synthesised sample by sample: a kick on every beat, a hi-hat
 * on the off-beats, a bass line on the eighths and a sustained minor-chord pad.
 *
 * Why synthetic and not a bundled track: a thumbnail has to show each style the way it looks
 * while music plays, so the sound engine needs every band it listens to (the kick's sub and
 * onset, the bass, the pad's mids, the hat's air) and a steady beat for the tempo tracker. A
 * track would add an asset, a licence and a decoder to a test whose only job is a picture. The
 * maths is pure and seeded, so every run (and every style) hears exactly the same audio, and a
 * re-render changes a thumbnail only when the style itself changed.
 *
 * Interleaved stereo float, both channels the same apart from a small pad spread.
 */
internal class SyntheticGroove(
    val sampleRateHz: Int = SAMPLE_RATE_HZ,
) {
    private var position = 0L
    private var noise = NOISE_SEED

    /** The next [frames] stereo frames, interleaved L/R. */
    fun next(frames: Int): FloatArray {
        val out = FloatArray(frames * CHANNELS)
        for (i in 0 until frames) {
            val t = (position + i).toDouble() / sampleRateHz
            val beatPos = t / BEAT_SECONDS
            val beat = beatPos.toLong()
            val inBeat = (beatPos - beat) * BEAT_SECONDS
            val eighth = (beatPos * 2).toLong()
            val inEighth = (beatPos * 2 - eighth) * BEAT_SECONDS / 2
            val drums = kick(inBeat) + hat(eighth, inEighth)
            val bass = bass(t, beat, inEighth)
            val padL = pad(t, 0.0)
            val padR = pad(t, PAD_SPREAD)
            out[i * 2] = clip(drums + bass + padL)
            out[i * 2 + 1] = clip(drums + bass + padR)
        }
        position += frames
        return out
    }

    // A sine whose pitch falls from KICK_HIGH_HZ to KICK_LOW_HZ in a few tens of ms: the classic
    // 808-ish body that puts energy below 100 Hz with a sharp onset for beat detection. The phase
    // is the integral of that exponential pitch glide, so the sweep has no clicks.
    private fun kick(s: Double): Double {
        val glide = (KICK_HIGH_HZ - KICK_LOW_HZ) * KICK_SWEEP_S * (1 - exp(-s / KICK_SWEEP_S))
        val phase = 2 * PI * (KICK_LOW_HZ * s + glide)
        return KICK_GAIN * exp(-s / KICK_DECAY_S) * sin(phase)
    }

    // Off-beat white noise bursts from a fixed LCG: high-band energy on the eighths between kicks.
    private fun hat(
        eighth: Long,
        s: Double,
    ): Double {
        noise = noise * LCG_MUL + LCG_ADD
        if (eighth % 2 == 0L) return 0.0
        val white = ((noise ushr NOISE_SHIFT) and NOISE_MASK).toDouble() / NOISE_MASK * 2 - 1
        return HAT_GAIN * exp(-s / HAT_DECAY_S) * white
    }

    // Root notes of an A minor - F - C - G progression, one bar each, plucked on every eighth.
    private fun bass(
        t: Double,
        beat: Long,
        inEighth: Double,
    ): Double {
        val hz = BASS_ROOTS_HZ[((beat / BEATS_PER_BAR) % BASS_ROOTS_HZ.size).toInt()]
        val env = exp(-inEighth / BASS_DECAY_S)
        val phase = 2 * PI * hz * t
        return BASS_GAIN * env * (sin(phase) + HARMONIC * sin(2 * phase))
    }

    // An A minor triad with a slow swell, so the mids never fall silent between hits.
    private fun pad(
        t: Double,
        detune: Double,
    ): Double {
        val swell = 0.5 + 0.5 * sin(2 * PI * t / (BEAT_SECONDS * BEATS_PER_BAR))
        return PAD_GAIN * swell * PAD_HZ.sumOf { sin(2 * PI * (it + detune) * t) } / PAD_HZ.size
    }

    private fun clip(v: Double): Float = v.coerceIn(-1.0, 1.0).toFloat()

    companion object {
        const val SAMPLE_RATE_HZ = 48_000
        const val CHANNELS = 2
        private const val BEAT_SECONDS = 0.5 // 120 BPM
        private const val BEATS_PER_BAR = 4
        private const val KICK_HIGH_HZ = 150.0
        private const val KICK_LOW_HZ = 48.0
        private const val KICK_SWEEP_S = 0.03
        private const val KICK_DECAY_S = 0.18
        private const val KICK_GAIN = 0.9
        private const val HAT_DECAY_S = 0.025
        private const val HAT_GAIN = 0.18
        private const val BASS_DECAY_S = 0.12
        private const val BASS_GAIN = 0.35
        private const val HARMONIC = 0.3
        private const val PAD_GAIN = 0.22
        private const val PAD_SPREAD = 0.7
        private val BASS_ROOTS_HZ = doubleArrayOf(55.0, 43.65, 65.41, 49.0)
        private val PAD_HZ = doubleArrayOf(220.0, 261.63, 329.63)
        private const val NOISE_SEED = 0x2545F4914F6CDD1DL
        private const val LCG_MUL = 6364136223846793005L
        private const val LCG_ADD = 1442695040888963407L
        private const val NOISE_SHIFT = 33
        private const val NOISE_MASK = 0x7FFFFFFFL
    }
}
