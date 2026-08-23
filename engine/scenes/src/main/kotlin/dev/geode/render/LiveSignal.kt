package dev.geode.render

import dev.geode.analysis.AudioFeatures
import kotlin.math.abs

/**
 * The live drive the visuals run on.
 *
 * WHY: the picture has to move on the signal as it arrives, frame by frame — not on a
 * tempo grid, a beat tracker's guess or an offline timeline. `transient` fires on the
 * frame an onset is heard and carries its strength; `rms`, `centroid`, the band
 * envelopes and the stereo field are all instantaneous too. Everything the scenes used
 * to take from `beat` / `beatStrength` / `bpm` / `sectionIndex` / `progress` is derived
 * from those here instead, so an un-analysed source (live input, a file the analyser has
 * not reached yet) reacts exactly like an analysed one, and nothing waits a bar to catch
 * up with what you can already hear.
 */
object LiveSignal {
    /**
     * Below this a transient is noise floor rather than a hit. [dev.geode.engine.audio.OnsetPeakPicker]
     * already applies its own refractory window, so this is only a magnitude gate.
     */
    const val HIT_FLOOR: Float = 0.06f

    /** Impulse for the frame a hit lands on, 0 otherwise. 0..1. */
    fun hit(f: AudioFeatures): Float = if (f.transient >= HIT_FLOOR) f.transient.coerceIn(0f, 1f) else 0f

    /** Overall level, 0..1. */
    fun level(f: AudioFeatures): Float = f.rms.coerceIn(0f, 1f)

    /** Spectral brightness — the normalized spectral centroid, 0 (dark) .. 1 (bright). */
    fun brightness(f: AudioFeatures): Float = f.centroid.coerceIn(0f, 1f)

    /** How wide the stereo image is right now, 0 (mono) .. 1 (fully decorrelated). */
    fun width(f: AudioFeatures): Float = f.stereoWidth.coerceIn(0f, 1f)

    /** Left/right movement, -1 (hard left) .. 1 (hard right). */
    fun pan(f: AudioFeatures): Float = f.stereoPan.coerceIn(-1f, 1f)

    /**
     * Rising-edge detector for hits.
     *
     * The same shape the scenes used for `beat && !prevBeat`, so a splat, a drop or a
     * re-seed fires once per hit rather than for every frame the impulse is above floor.
     */
    class Edge {
        private var armed = true

        fun reset() {
            armed = true
        }

        fun step(f: AudioFeatures): Boolean {
            val hot = f.transient >= HIT_FLOOR
            val fired = hot && armed
            armed = !hot
            return fired
        }
    }

    /**
     * A live stand-in for "how far through the piece are we".
     *
     * The fluid journeys used to walk the track's own play position and
     * its pre-analysed section list, which meant nothing moved on live input and a
     * seek teleported the layout. This walks on heard energy instead: loud passages
     * advance it, quiet ones let it drift back, and a sustained change of spectral
     * brightness — a chorus arriving, an instrument dropping out — counts as a new
     * section and re-seats the layout. All of it from the current frame.
     */
    class Traverse {
        /** 0..1, the journey position the paths are laid out along. */
        var position: Float = 0f
            private set

        /** Increments whenever the material changes character enough to re-seat a layout. */
        var sectionCount: Int = 0
            private set

        private var brightnessMean = -1f
        private var settleSeconds = 0f

        fun reset() {
            position = 0f
            sectionCount = 0
            brightnessMean = -1f
            settleSeconds = 0f
        }

        fun step(
            f: AudioFeatures,
            dt: Float,
        ) {
            val drive = level(f)
            // Loud advances, near-silence eases back, so the layout keeps moving through a
            // long track without ever needing to know how long the track is.
            val rate = (drive - IDLE_LEVEL) / TRAVERSE_SECONDS
            position = (position + rate * dt).coerceIn(0f, 1f)

            val bright = brightness(f)
            if (brightnessMean < 0f) brightnessMean = bright
            settleSeconds += dt
            if (settleSeconds >= SECTION_REFRACTORY_SECONDS && abs(bright - brightnessMean) >= SECTION_SHIFT) {
                sectionCount++
                settleSeconds = 0f
                brightnessMean = bright
            }
            brightnessMean += (bright - brightnessMean) * (dt / BRIGHTNESS_TAU_SECONDS).coerceAtMost(1f)
        }

        private companion object {
            /** Level below which the traverse drifts back rather than forward. */
            const val IDLE_LEVEL = 0.12f

            /** Seconds of sustained full level to walk the whole journey once. */
            const val TRAVERSE_SECONDS = 150f

            const val BRIGHTNESS_TAU_SECONDS = 8f

            /** How far the centroid has to move from its running mean to count as a new section. */
            const val SECTION_SHIFT = 0.14f

            const val SECTION_REFRACTORY_SECONDS = 12f
        }
    }
}
