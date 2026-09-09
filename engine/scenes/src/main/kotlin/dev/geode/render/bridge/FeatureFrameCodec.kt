package dev.geode.render.bridge

import dev.geode.analysis.AudioFeatures
import dev.geode.engine.bridge.FeatureFrameLayout

/** Lays an [AudioFeatures] out as a `GeodeFeatureFrame`; every scalar the native frame has is carried. */
object FeatureFrameCodec {
    fun pack(
        f: AudioFeatures,
        out: FloatArray,
    ) {
        check(out.size >= FeatureFrameLayout.FLOATS) {
            "feature frame needs ${FeatureFrameLayout.FLOATS} floats, got ${out.size}"
        }
        out.fill(0f, 0, FeatureFrameLayout.FLOATS)
        out[FeatureFrameLayout.RMS] = f.rms
        out[FeatureFrameLayout.BASS] = f.bass
        out[FeatureFrameLayout.MID] = f.mid
        out[FeatureFrameLayout.TREBLE] = f.treble
        out[FeatureFrameLayout.CENTROID] = f.centroid
        out[FeatureFrameLayout.FLUX] = f.flux
        out[FeatureFrameLayout.ONSET] = f.onset
        out[FeatureFrameLayout.BEAT] = if (f.beat) 1f else 0f
        out[FeatureFrameLayout.BEAT_STRENGTH] = f.beatStrength
        out[FeatureFrameLayout.TRANSIENT] = f.transient
        out[FeatureFrameLayout.BEAT_PHASE] = f.beatPhase
        out[FeatureFrameLayout.PULSE_CONFIDENCE] = f.pulseConfidence
        out[FeatureFrameLayout.BPM] = f.bpm
        out[FeatureFrameLayout.TEMPO_STABILITY] = f.tempoStability
        out[FeatureFrameLayout.BAR_PHASE] = f.barPhase
        out[FeatureFrameLayout.BEAT_IN_BAR] = f.beatInBar.toFloat()
        out[FeatureFrameLayout.DOWNBEAT] = if (f.downbeat) 1f else 0f
        out[FeatureFrameLayout.DOWNBEAT_CONFIDENCE] = f.downbeatConfidence
        out[FeatureFrameLayout.MACRO_ENERGY] = f.macroEnergy
        out[FeatureFrameLayout.KICK] = f.kick
        out[FeatureFrameLayout.SNARE] = f.snare
        out[FeatureFrameLayout.HAT] = f.hat
        out[FeatureFrameLayout.NOVELTY] = f.novelty
        out[FeatureFrameLayout.SECTION_BOUNDARY] = if (f.sectionBoundary) 1f else 0f
        out[FeatureFrameLayout.BUILDUP] = f.buildup
        out[FeatureFrameLayout.DROP] = if (f.drop) 1f else 0f
        out[FeatureFrameLayout.ARRIVAL] = if (f.arrival) 1f else 0f
        out[FeatureFrameLayout.HARMONICITY] = f.harmonicity
        out[FeatureFrameLayout.WARMUP] = f.warmup
        out[FeatureFrameLayout.STEREO_WIDTH] = f.stereoWidth
        out[FeatureFrameLayout.STEREO_CORRELATION] = f.stereoCorrelation
        out[FeatureFrameLayout.STEREO_PAN] = f.stereoPan
        out[FeatureFrameLayout.CHROMA_CONFIDENCE] = f.chromaConfidence
        f.bands.copyInto(out, FeatureFrameLayout.BANDS, 0, minOf(f.bands.size, FeatureFrameLayout.BAND_COUNT))
        f.waveform.copyInto(out, FeatureFrameLayout.WAVEFORM, 0, minOf(f.waveform.size, FeatureFrameLayout.WAVEFORM_POINTS))
        if (f.hasChroma) f.chroma.copyInto(out, FeatureFrameLayout.CHROMA, 0, FeatureFrameLayout.CHROMA_BINS)
    }
}
