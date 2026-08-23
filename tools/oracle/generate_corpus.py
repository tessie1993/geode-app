#!/usr/bin/env python3
"""Generates the audio fixture corpus and its expected values.

MASTER_PLAN.md V2-3-01. Runs OUTSIDE the app: librosa is ORACLE tier
(provenance.json), which means it produces expected values here and never
enters the runtime. Nothing this script imports may appear in the APK.

Usage:
    python3 -m venv .venv && .venv/bin/pip install librosa soundfile
    .venv/bin/python tools/oracle/generate_corpus.py

Output (under app/src/test/resources/corpus/):
    <name>.pcm        raw interleaved little-endian int16, no header
    manifest.json     rates, channels, checksums, expected values, tolerances

Raw int16 rather than WAV because the Kotlin side then needs no decoder and no
header parser - the manifest is the only description of the bytes, so there is
exactly one place for the two sides to disagree, and a checksum over it. int16
rather than float32 because it is what the app's tap actually delivers, and it
halves a corpus that has to live in the repository. Both sides dequantise the
same way, x / 32768, so both analyse identical numbers.

Determinism is a requirement, not a nicety: the expected values describe these
exact bytes. Every random source is seeded, and the checksums in the manifest
fail the build if a regenerated corpus differs.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys

import librosa
import numpy as np
import scipy
import soundfile

# Bumped by hand whenever a fixture's definition changes, so a stale corpus is
# a visible mismatch rather than a silent one.
GENERATOR_VERSION = 5

# The STFT the per-frame expectations are computed over. Matches
# AnalysisBranch.GENERAL, and librosa's center=True / pad_mode="constant"
# framing is FrameGrid's convention exactly - verified, not assumed: frame k
# centred at k*hop with zeros outside the signal reproduces librosa's own
# stft to 8e-8 relative.
FRAME_N_FFT = 1024
FRAME_HOP = 512

# librosa's spectral_rolloff default.
ROLLOFF_FRACTION = 0.85

# The timbre block, V2-3-05a. One definition per feature, stated here and
# implemented by the Kotlin side:
#   MFCC: HTK-mel (2595 log10(1 + f/700)) triangular filters, unit peak
#   (librosa.filters.mel htk=True norm=None), over the POWER spectrum;
#   10*log10 with a 1e-10 floor and NO top_db clipping (top_db needs the whole
#   spectrogram's maximum, which a causal engine cannot see); orthonormal
#   DCT-II; first N_MFCC coefficients.
#   Timbre flux: L2 distance between successive MFCC vectors EXCLUDING c0
#   (c0 is level, and level change is what `flux` already measures).
#   Spectral contrast: octave bands from CONTRAST_FMIN, peak minus valley in
#   dB where peak/valley are the means of the top/bottom CONTRAST_ALPHA
#   fraction of the band's power bins (at least one bin each).
N_MELS = 40
N_MFCC = 13
CONTRAST_BANDS = 6
CONTRAST_FMIN = 200.0
CONTRAST_ALPHA = 0.02
LOG_POWER_FLOOR = 1e-10

SR = 22_050
OUT = pathlib.Path(__file__).resolve().parents[2] / "app/src/test/resources/corpus"

# Per-feature agreement required between this oracle and the Kotlin engine.
# Each is a decision, not a default:
TOLERANCES = {
    # Correlation is a normalised sum over the whole window; the only
    # divergence is float summation order.
    "stereoCorrelation": 1e-4,
    # Width is a ratio of RMS magnitudes, one sqrt more than correlation.
    "stereoWidth": 1e-4,
    # A ratio of channel RMS magnitudes, same exposure as width.
    "stereoPan": 1e-4,
    # Plain sqrt(mean(x^2)) over the fixture.
    "rms": 1e-6,
    # The band a tone lands in is an integer; a tone must not straddle.
    "peakBandExact": 0.0,
    # Per-frame descriptors. Kotlin accumulates in double over the same bins,
    # so the only divergence is summation order; the looser rolloff bound is
    # because it is a bin index, and a frame whose energy sits exactly on a
    # threshold can legitimately pick either side.
    "centroidHz": 1e-4,
    # Two orders looser than the centroid, and measured rather than guessed.
    # Bandwidth is a second moment about the centroid, so its (f - c)^2 weight
    # puts almost all the leverage on the near-zero high bins - the ones where
    # two FFT implementations disagree most in relative terms. For the AM
    # fixture, bins above 5 kHz carry 86.6% of the second moment while holding
    # 0.0168% of the magnitude. Perturbing a spectrum by float32 epsilon times
    # its largest bin - how JTransforms and numpy actually differ - moves the
    # centroid by 4e-5 and the bandwidth by 5.5e-3. A wrong formula is out by
    # tens of percent, so 1e-2 still catches every real error.
    "bandwidthHz": 1e-2,
    "rolloffHz": 1e-9,
    "flatness": 1e-5,
    "flux": 1e-4,
    "zeroCrossingRate": 1e-9,
    "frameRms": 1e-6,
    "framePeak": 1e-9,
    # MEASURED, per the bandwidth precedent, not guessed. The exposure is
    # float32-vs-float64 on near-zero bins: a leakage bin differs relatively
    # most exactly where the log then amplifies it - the corpus worst case is
    # 2.2% relative (contrast band 3 of am_4hz; mfcc c1 of the sweep is
    # 2.1%; the splice fixture, whose bands hold only leakage, reaches 5.3%).
    # 0.08 clears that; fault injections below prove a wrong mel scale, DCT
    # norm or alpha is out by whole dB to tens.
    "mfcc": 0.08,
    "timbreFlux": 0.08,
    "spectralContrast": 0.08,
}


def _tone(freq: float, seconds: float, amp: float = 0.5) -> np.ndarray:
    t = np.arange(int(SR * seconds), dtype=np.float64) / SR
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def _midi_hz(midi: int) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def _chord(midis: list[int], seconds: float, amp: float = 0.18) -> np.ndarray:
    """Equal-tempered sine sum. Pure sines: no harmonics, so spectral leakage
    is the only cross-bin energy and the expected pitch classes are
    unambiguous by construction."""
    out = np.zeros(int(SR * seconds), dtype=np.float64)
    for m in midis:
        out += _tone(_midi_hz(m), seconds, amp).astype(np.float64)
    return out.astype(np.float32)


def _melody(midis: list[int], note_seconds: float, fade_seconds: float = 0.01) -> np.ndarray:
    """One note after another with raised-cosine joins, so the splices stay
    harmony rather than becoming percussion (and do not trip the measured
    per-frame tolerances the way the hard-splice fixture does)."""
    notes = []
    fade = int(SR * fade_seconds)
    envelope_edge = 0.5 - 0.5 * np.cos(np.pi * np.arange(fade) / fade)
    for m in midis:
        note = _tone(_midi_hz(m), note_seconds, amp=0.4).astype(np.float64)
        note[:fade] *= envelope_edge
        note[-fade:] *= envelope_edge[::-1]
        notes.append(note)
    return np.concatenate(notes).astype(np.float32)


# Ground truth for the harmony fixtures, known by construction: pitch classes
# with 0 = C, and the key a musician would name. These are product
# expectations for the app's chromagram and key detector, not numeric parity
# with any library's chroma implementation.
HARMONY_EXPECTED = {
    "tone_440": {"dominantPitchClasses": [9]},
    "triad_c_major": {"dominantPitchClasses": [0, 4, 7], "key": "C major"},
    "triad_a_minor": {"dominantPitchClasses": [9, 0, 4], "key": "A minor"},
    "arpeggio_g_major": {"key": "G major"},
}


def _fixtures() -> dict[str, np.ndarray]:
    """name -> (frames, channels) float32, mono kept as (frames, 1)."""
    rng = np.random.default_rng(0xC0FFEE)
    out: dict[str, np.ndarray] = {}

    out["silence"] = np.zeros((SR // 2, 1), dtype=np.float32)

    impulse = np.zeros(SR // 2, dtype=np.float32)
    impulse[SR // 4] = 1.0
    out["impulse"] = impulse.reshape(-1, 1)

    out["tone_440"] = _tone(440.0, 0.5).reshape(-1, 1)

    sweep = librosa.chirp(fmin=110.0, fmax=4_000.0, sr=SR, duration=1.0).astype(np.float32)
    out["sweep"] = (0.5 * sweep).reshape(-1, 1)

    # Amplitude modulation: a 440 Hz carrier at 4 Hz, the shape a tremolo makes
    # and the one an envelope follower has to track without chattering.
    carrier = _tone(440.0, 1.5, amp=1.0)
    t = np.arange(carrier.size, dtype=np.float64) / SR
    out["am_4hz"] = (carrier * (0.5 + 0.5 * np.sin(2 * np.pi * 4.0 * t)).astype(np.float32) * 0.5).reshape(-1, 1)

    out["clicks_120bpm"] = librosa.clicks(
        times=np.arange(0.0, 4.0, 0.5), sr=SR, length=int(SR * 4)
    ).astype(np.float32).reshape(-1, 1)

    # Tempo ramp 90 -> 150 BPM: beat trackers that lock to a fixed grid follow
    # this badly, which is the point of having it.
    times, now, bpm = [], 0.0, 90.0
    while now < 6.0:
        times.append(now)
        now += 60.0 / bpm
        bpm += 4.0
    out["tempo_ramp"] = librosa.clicks(times=np.array(times), sr=SR, length=int(SR * 6)).astype(np.float32).reshape(-1, 1)

    # Stereo: hard anti-phase, the case that collapses to silence in mono.
    mono = _tone(300.0, 1.0)
    out["stereo_antiphase"] = np.stack([mono, -mono], axis=1)

    # Partially decorrelated: shared centre plus independent noise per side.
    noise_l = rng.normal(0, 0.15, mono.size).astype(np.float32)
    noise_r = rng.normal(0, 0.15, mono.size).astype(np.float32)
    out["stereo_wide"] = np.stack([mono + noise_l, mono + noise_r], axis=1)

    out["stereo_identical"] = np.stack([mono, mono], axis=1)

    # Hard-panned: everything on the left, the case pan exists to name and
    # the one width deliberately reads as 0.5 rather than as a direction.
    out["tone_panned_left"] = np.stack([mono, np.zeros_like(mono)], axis=1)

    # A hard splice: 300 Hz cut mid-cycle into 900 Hz, no crossfade. Stands in
    # for the discontinuous stream the plan asks for - a seek, a source change,
    # a dropped buffer.
    a = _tone(300.0, 0.5)
    b = _tone(900.0, 0.5)
    out["discontinuity"] = np.concatenate([a, b]).reshape(-1, 1)

    # The harmony fixtures; expectations in HARMONY_EXPECTED. C4-E4-G4,
    # A3-C4-E4, and the G major scale over one octave.
    out["triad_c_major"] = _chord([60, 64, 67], 1.0).reshape(-1, 1)
    out["triad_a_minor"] = _chord([57, 60, 64], 1.0).reshape(-1, 1)
    out["arpeggio_g_major"] = _melody([67, 69, 71, 72, 74, 76, 78, 79], 0.25).reshape(-1, 1)

    return out


def _quantise(data: np.ndarray) -> np.ndarray:
    """To int16 and back, so the oracle analyses exactly what Kotlin will read."""
    clipped = np.clip(data, -1.0, 32767.0 / 32768.0)
    return (np.rint(clipped * 32768.0).astype("<i2").astype(np.float32) / 32768.0)


def _per_frame(mono: np.ndarray) -> dict:
    """Descriptor values per STFT frame, for a pointwise comparison.

    Aggregates hide the failures that matter: a formula wrong only at the edges
    of the spectrum, or a framing off by one hop, both leave the mean roughly
    right. These are the arrays the Kotlin side is checked against frame by
    frame.

    Every formula here was validated against librosa's own feature functions
    before being written down - centroid, bandwidth, rolloff and flatness all
    agree to float precision or exactly. They are recomputed rather than called
    so the manifest states one definition per feature, in one place, which is
    what the Kotlin side is actually implementing.
    """
    spec = np.abs(librosa.stft(mono, n_fft=FRAME_N_FFT, hop_length=FRAME_HOP))
    freqs = np.fft.rfftfreq(FRAME_N_FFT, 1.0 / SR)
    total = spec.sum(axis=0)
    live = total > 1e-12

    centroid = np.zeros_like(total)
    centroid[live] = (freqs[:, None] * spec).sum(axis=0)[live] / total[live]

    bandwidth = np.zeros_like(total)
    deviation = (spec * (freqs[:, None] - centroid[None, :]) ** 2).sum(axis=0)
    bandwidth[live] = np.sqrt(deviation[live] / total[live])

    rolloff = np.zeros_like(total)
    cumulative = np.cumsum(spec, axis=0)
    hit = (cumulative >= (ROLLOFF_FRACTION * total)[None, :]).argmax(axis=0)
    rolloff[live] = freqs[hit][live]

    # Power spectrum with librosa's floor: geometric over arithmetic mean.
    power = np.maximum(spec**2, 1e-10)
    flatness = np.exp(np.mean(np.log(power), axis=0)) / np.mean(power, axis=0)

    # Half-wave rectified L1 difference between successive magnitude spectra,
    # normalised by bin count. Frame 0 has no predecessor and is 0.
    rise = np.diff(spec, axis=1)
    flux = np.zeros_like(total)
    flux[1:] = np.maximum(rise, 0.0).sum(axis=0) / spec.shape[0]

    # Zero crossings over the same frames, WITHOUT librosa's phantom: its
    # zero_crossing_rate forces index 0 to count as a crossing whatever the
    # sample is, then divides by frame_length rather than frame_length - 1.
    # That is an API convention, not a definition, and it inflates a quiet
    # frame's rate by 1/1024. Counted honestly here.
    padded = np.pad(mono, FRAME_N_FFT // 2)
    frames = librosa.util.frame(padded, frame_length=FRAME_N_FFT, hop_length=FRAME_HOP)
    signs = np.signbit(frames)
    crossings = (signs[1:, :] != signs[:-1, :]).sum(axis=0)
    zcr = crossings / (FRAME_N_FFT - 1)

    rms = np.sqrt((frames.astype(np.float64) ** 2).mean(axis=0))
    peak = np.abs(frames).max(axis=0)

    # ---- the timbre block; see the constants' comment for the definitions --
    mel_fb = librosa.filters.mel(sr=SR, n_fft=FRAME_N_FFT, n_mels=N_MELS, htk=True, norm=None)
    mel_power = mel_fb @ (spec.astype(np.float64) ** 2)
    log_mel = 10.0 * np.log10(np.maximum(mel_power, LOG_POWER_FLOOR))
    mfcc = scipy.fft.dct(log_mel, axis=0, type=2, norm="ortho")[:N_MFCC]

    timbre_flux = np.zeros_like(total)
    timbre_flux[1:] = np.sqrt((np.diff(mfcc[1:, :], axis=1) ** 2).sum(axis=0))

    contrast = np.zeros((CONTRAST_BANDS, len(total)))
    power_bins = spec.astype(np.float64) ** 2
    for b in range(CONTRAST_BANDS):
        lo, hi = CONTRAST_FMIN * 2.0**b, CONTRAST_FMIN * 2.0 ** (b + 1)
        sel = (freqs >= lo) & (freqs < hi)
        band = np.sort(power_bins[sel, :], axis=0)
        k = max(1, int(CONTRAST_ALPHA * band.shape[0]))
        valley = 10.0 * np.log10(np.maximum(band[:k, :].mean(axis=0), LOG_POWER_FLOOR))
        peak_db = 10.0 * np.log10(np.maximum(band[-k:, :].mean(axis=0), LOG_POWER_FLOOR))
        contrast[b] = peak_db - valley

    count = min(len(total), spec.shape[1])

    def trim(values: np.ndarray) -> list[float]:
        return [round(float(v), 9) for v in values[:count]]

    def trim2(matrix: np.ndarray) -> list[list[float]]:
        return [[round(float(v), 6) for v in matrix[:, k]] for k in range(count)]

    return {
        "nFft": FRAME_N_FFT,
        "hop": FRAME_HOP,
        "rolloffFraction": ROLLOFF_FRACTION,
        "frames": count,
        "centroidHz": trim(centroid),
        "bandwidthHz": trim(bandwidth),
        "rolloffHz": trim(rolloff),
        "flatness": trim(flatness),
        "flux": trim(flux),
        "zeroCrossingRate": trim(zcr[:count]),
        "frameRms": trim(rms[:count]),
        "framePeak": trim(peak[:count]),
        "nMels": N_MELS,
        "nMfcc": N_MFCC,
        "contrastBands": CONTRAST_BANDS,
        "contrastFminHz": CONTRAST_FMIN,
        "contrastAlpha": CONTRAST_ALPHA,
        "mfcc": trim2(mfcc),
        "timbreFlux": trim(timbre_flux),
        "spectralContrast": trim2(contrast),
    }


def _expected(name: str, data: np.ndarray) -> dict:
    """What the oracle says about this fixture."""
    mono = data.mean(axis=1)
    rms = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
    exp: dict = {
        "rms": rms,
        "spectralCentroidHz": (
            float(np.mean(librosa.feature.spectral_centroid(y=mono, sr=SR))) if rms > 0 else 0.0
        ),
        "zeroCrossingRate": float(np.mean(librosa.feature.zero_crossing_rate(y=mono))),
    }

    if data.shape[1] == 2:
        left = data[:, 0].astype(np.float64)
        right = data[:, 1].astype(np.float64)

        # Correlation from L and R directly - the textbook definition. This is
        # the half that is a real oracle: StereoField reaches the same number
        # through mid/side identities (sum(L*R) = sum(m^2) - sum(s^2), and so
        # on) to avoid reconstructing the channels, and if that algebra were
        # wrong nothing inside the app would notice. Computed here the obvious
        # way, in another language, over the same bytes.
        num = float(np.sum(left * right))
        den = np.sqrt(float(np.sum(left * left)) * float(np.sum(right * right)))
        # Silence reads 1, not 0: a silent passage is not "decorrelated".
        exp["stereoCorrelation"] = float(num / den) if den > 0 else 1.0

        # Width is this project's own quantity, not a standard one, so the
        # oracle mirrors its definition - s / (m + s) over RMS magnitudes - and
        # checks transcription and indexing rather than the definition. Note
        # what the naive ratio sqrt(ss)/sqrt(mm) would do here: anti-phase has
        # no mid at all, so it would divide by zero and report the WIDEST
        # possible signal as width 0. It did, before this comment existed.
        mid = (left + right) / 2.0
        side = (left - right) / 2.0
        m = float(np.sqrt(np.mean(mid * mid)))
        s = float(np.sqrt(np.mean(side * side)))
        exp["stereoWidth"] = float(s / (m + s)) if (m + s) > 1e-9 else 0.0

        # Pan: the L/R RMS balance, -1 fully left to +1 fully right, 0 for a
        # centred or silent signal. Computed from the channels directly; the
        # Kotlin side reaches the same number through mid/side identities.
        rms_l = float(np.sqrt(np.mean(left * left)))
        rms_r = float(np.sqrt(np.mean(right * right)))
        exp["stereoPan"] = float((rms_r - rms_l) / (rms_l + rms_r)) if (rms_l + rms_r) > 1e-9 else 0.0

    if name.startswith(("clicks", "tempo")):
        tempo, _ = librosa.beat.beat_track(y=mono, sr=SR)
        exp["tempoBpm"] = float(np.atleast_1d(tempo)[0])

    exp.update(HARMONY_EXPECTED.get(name, {}))

    return exp


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("*.pcm"):
        stale.unlink()

    entries = []
    for name, source in sorted(_fixtures().items()):
        data = _quantise(source)
        raw = np.ascontiguousarray(np.rint(np.clip(data, -1.0, 32767.0 / 32768.0) * 32768.0), dtype="<i2").tobytes()
        (OUT / f"{name}.pcm").write_bytes(raw)
        entries.append(
            {
                "name": name,
                "file": f"{name}.pcm",
                "sampleRateHz": SR,
                "channels": int(data.shape[1]),
                "frames": int(data.shape[0]),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "expected": _expected(name, data),
                "perFrame": _per_frame(np.ascontiguousarray(data.mean(axis=1), dtype=np.float32)),
            }
        )

    manifest = {
        "generatorVersion": GENERATOR_VERSION,
        "note": (
            "Generated by tools/oracle/generate_corpus.py. librosa is ORACLE tier: it produces "
            "these values and never enters the runtime. Regenerate rather than hand-edit."
        ),
        "libraries": {
            "python": sys.version.split()[0],
            "librosa": librosa.__version__,
            "numpy": np.__version__,
            "scipy": scipy.__version__,
            "soundfile": soundfile.__version__,
        },
        "sampleFormat": "raw interleaved little-endian int16, no header; divide by 32768 for float",
        "tolerances": TOLERANCES,
        "notCovered": [
            "short real-music excerpts - none are available to this generator, and a "
            "corpus that claimed them without shipping them would be worse than the gap",
            "libebur128 loudness reference values - a second oracle, its own slice",
        ],
        "fixtures": entries,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    total = sum(len((OUT / e["file"]).read_bytes()) for e in entries)
    print(f"{len(entries)} fixtures, {total / 1024:.0f} KiB, librosa {librosa.__version__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
