#!/usr/bin/env python3
"""Geode's analysis lab: labelled clips, librosa goldens and mir_eval reference scores.

    pip install -r tools/analysis_lab/requirements.txt
    python3 tools/analysis_lab/lab.py --out build/analysis-lab

Why it exists (#462, spec #455 § Analysis): Geode has two analysers, the causal live one (core/sound, what
the screen, wallpaper and dream hear) and the whole-track offline one an export uses (core/sound/offline).
Both must only get better, so CI scores them against music whose answers are known, and the offline one,
which ports librosa's methods, is also held to librosa's own output on the same audio.

What it writes into --out (nothing here is committed; CI rebuilds it on every run):
  clips.txt            the clip names, one per line, in bench order
  manifest.tsv         clip, source, licence, sha256 of the WAV (every clip must be redistributable)
  <clip>.wav           44.1 kHz stereo PCM16
  <clip>.truth.tsv     the answers, exact by construction: tempo, beats, downbeats, section boundaries, key
  <clip>.librosa.tsv   librosa's answers (the reference analyser): tempo, beats, key, boundaries
  <clip>.onset.tsv     librosa's onset strength envelope, one value per frame (what the offline port matches)
  <clip>.features.tsv  librosa's frame features (#464): rms, spectral centroid, rolloff and flatness, mean
                       onset strength, HPSS harmonic and percussive energy, CQT chroma (tuning 0)
  <clip>.onsets.tsv    librosa.onset.onset_detect's onset times on the mean onset strength
  <clip>.mir_eval.tsv  mir_eval's scores of librosa's answers against the truth; the C++ bench recomputes
                       them with its own metrics and fails if the two disagree, so the bench's maths is
                       pinned to mir_eval's conventions without Python in the C++ path
  versions.tsv         the library versions that made them

The clips are synthesized here from a score, never recorded (fosfora's bench/make_fixture.py idea, report
docs/research/fosfora/03-offline-bench-ci.md R4): the ground truth comes from the same score as the audio, so
it is exact, and there is no licence question. A real clip may join only with a row in manifest.tsv beside
this file that names its licence and checksum; the lab refuses any clip without one.

Analysis parameters: librosa's defaults are a 512-sample hop and a 2048-point FFT at 22.05 kHz. The clips are
44.1 kHz and are not resampled (a resampler would be one more thing to port), so the hop and FFT double to
1024 and 4096: the same ~43 frames a second and ~93 ms window, the same as OfflineBeats::paramsFor in C++.
"""
import argparse
import hashlib
import os
import sys
import zlib

import numpy as np
import scipy.io.wavfile

SR = 44100
HOP = 1024
N_FFT = 4096
LEAD = 0.25  # seconds of silence before the first beat
TAIL = 1.0  # seconds of ring-out after the last bar
TIME_DECIMALS = 6  # times are rounded once, then written and scored from the same rounded values

PITCH_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
CHORD_STEPS = {"maj": (0, 4, 7), "min": (0, 3, 7)}

# Krumhansl–Kessler key profiles: the classic template for a key from a mean chroma, the same family of
# profiles live Harmony matches against.
KK_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KK_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def section(name, bars, **parts):
    """One section of a score. Parts: click, kick, snare, rim, bass (beat lists, or "eighth"), hats
    ("eighth" or "sixteenth"), open_hat (on every off-beat), pad, bright, roll, riser, crash, gain."""
    return dict(name=name, bars=bars, **parts)


# Every clip, in bench order. Each answers one question about an analyser:
#   click_120          pure beat tracking: a click per beat, the downbeat louder and higher
#   groove_96_Dmaj     a plain pop groove, one chord a bar: beats, downbeats, a major key
#   song_124_Am        a whole song (intro, verse, build, drop, outro): sections and the drop, a minor key
#   offbeat_140_Emin   four on the floor with an open hat and the bass on every "and": the known hard case
#                      (docs/research/fosfora/02-rhythm-structure.md §4)
#   ballad_72_Ebmaj    slow and soft, verse to chorus: a slow tempo, a timbre boundary, a flat key
#   dnb_172_Fmin       a fast syncopated kick (1 and 3-and): tempo octave errors
CLIPS = [
    dict(name="click_120", bpm=120, key=None, chords=None, sections=[
        section("clicks", 16, click=True, gain=1.0),
    ]),
    dict(name="groove_96_Dmaj", bpm=96, key=(2, "major"),
         chords=[(2, "maj"), (9, "maj"), (11, "min"), (7, "maj")], sections=[
        section("groove", 16, kick=[0, 2], snare=[1, 3], hats="eighth", bass=[0, 2], pad=True, gain=0.8),
    ]),
    dict(name="song_124_Am", bpm=124, key=(9, "minor"),
         chords=[(9, "min"), (2, "min"), (4, "maj"), (9, "min")], sections=[
        section("intro", 8, pad=True, hats="eighth", gain=0.5),
        section("verse", 8, kick=[0, 2], snare=[1, 3], hats="eighth", bass=[0, 2], pad=True, gain=0.75),
        section("build", 4, pad=True, roll=True, riser=True, gain=0.7),
        section("drop", 8, kick=[0, 1, 2, 3], snare=[1, 3], hats="sixteenth", open_hat=True, bass="eighth",
                pad=True, bright=True, crash=True, gain=1.0),
        section("outro", 4, pad=True, gain=0.45),
    ]),
    dict(name="offbeat_140_Emin", bpm=140, key=(4, "minor"),
         chords=[(4, "min"), (0, "maj"), (7, "maj"), (2, "maj")], sections=[
        section("groove", 16, kick=[0, 1, 2, 3], open_hat=True, bass=[0.5, 1.5, 2.5, 3.5], pad=True, gain=0.85),
    ]),
    dict(name="ballad_72_Ebmaj", bpm=72, key=(3, "major"),
         chords=[(3, "maj"), (10, "maj"), (0, "min"), (8, "maj")], sections=[
        section("verse", 8, pad=True, bass=[0], rim=[2], gain=0.55),
        section("chorus", 8, pad=True, bright=True, bass=[0, 2], kick=[0, 2], snare=[1, 3], hats="eighth",
                crash=True, gain=0.9),
    ]),
    dict(name="dnb_172_Fmin", bpm=172, key=(5, "minor"),
         chords=[(5, "min"), (1, "maj"), (8, "maj"), (3, "maj")], sections=[
        section("groove", 24, kick=[0, 2.5], snare=[1, 3], hats="eighth", bass=[0, 2.5], pad=True, gain=0.85),
    ]),
]


# ---- Instruments -----------------------------------------------------------------------------------------
# Plain additive and noise voices. They only need to be clearly what they are (a kick has its low sweep, a
# snare its noise and body), with no randomness that would change between numpy versions beyond the pinned
# Generator.

def _t(seconds):
    return np.arange(int(round(seconds * SR))) / SR


def kick():
    t = _t(0.35)
    freq = 45.0 + 75.0 * np.exp(-t / 0.04)
    body = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t / 0.12)
    click = np.exp(-t / 0.002) * 0.3
    return body + click


def snare(rng, gain=1.0):
    t = _t(0.2)
    noise = np.diff(rng.standard_normal(len(t) + 1)) * 0.35 * np.exp(-t / 0.05)
    body = np.sin(2 * np.pi * 185.0 * t) * np.exp(-t / 0.03) * 0.5
    return (noise + body) * gain


def hat(rng, open_hat=False):
    t = _t(0.25 if open_hat else 0.05)
    noise = np.diff(rng.standard_normal(len(t) + 2), n=2) * 0.12
    return noise * np.exp(-t / (0.09 if open_hat else 0.015))


def crash(rng):
    t = _t(1.5)
    return np.diff(rng.standard_normal(len(t) + 1)) * 0.25 * np.exp(-t / 0.5)


def click(rng, freq, gain):
    """A metronome click: a pitched ping plus a few milliseconds of noise. The noise matters: librosa's beat
    tracker takes the median over 128 mel bands, and a pure tone moves too few of them to register."""
    t = _t(0.03)
    ping = np.sin(2 * np.pi * freq * t) * np.exp(-t / 0.008)
    noise = rng.standard_normal(len(t)) * 0.5 * np.exp(-t / 0.004)
    return (ping + noise) * gain


def tone(freqs, seconds, attack=0.01, release=0.08, harmonics=3, gain=1.0):
    """A chord or note: each pitch with a few decaying harmonics, a linear attack and a short release."""
    t = _t(seconds)
    out = np.zeros_like(t)
    for f in freqs:
        for k in range(1, harmonics + 1):
            out += np.sin(2 * np.pi * f * k * t) / (k * k)
    env = np.minimum(1.0, t / attack) * np.minimum(1.0, (seconds - t) / release).clip(0.0, 1.0)
    return out * env * gain / max(1, len(freqs))


def riser(rng, seconds):
    t = _t(seconds)
    noise = np.diff(rng.standard_normal(len(t) + 1)) * 0.2
    return noise * (t / seconds) ** 2


def midi_hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def chord_notes(root_pc, quality, octave_base):
    return [midi_hz(octave_base + root_pc + step) for step in CHORD_STEPS[quality]]


# ---- The score -------------------------------------------------------------------------------------------

def render(clip):
    """Renders one clip and its truth. Returns (stereo float array, truth dict)."""
    bpm = clip["bpm"]
    beat = 60.0 / bpm
    bar = 4 * beat
    total_bars = sum(s["bars"] for s in clip["sections"])
    duration = LEAD + total_bars * bar + TAIL
    n = int(round(duration * SR))
    out = np.zeros((n, 2))
    rng = np.random.default_rng(zlib.crc32(clip["name"].encode()))

    def place(t0, sig, pan=0.0, gain=1.0):
        start = int(round(t0 * SR))
        end = min(n, start + len(sig))
        if end <= start:
            return
        piece = sig[: end - start] * gain
        out[start:end, 0] += piece * (1.0 - 0.3 * pan)
        out[start:end, 1] += piece * (1.0 + 0.3 * pan)

    truth_beats, truth_downbeats, boundaries = [], [], []
    drums = {"kick": [], "snare": [], "hat": []}  # every drum hit the score places, for the drum F-measures
    bar_index = 0
    for s_index, s in enumerate(clip["sections"]):
        if s_index > 0:
            boundaries.append(LEAD + bar_index * bar)
        g = s.get("gain", 1.0)
        for b in range(s["bars"]):
            t_bar = LEAD + (bar_index + b) * bar
            chord = clip["chords"][(bar_index + b) % len(clip["chords"])] if clip["chords"] else None
            for k in range(4):
                truth_beats.append(t_bar + k * beat)
            truth_downbeats.append(t_bar)
            if s.get("click"):
                for k in range(4):
                    place(t_bar + k * beat, click(rng, 1500.0, 1.0) if k == 0 else click(rng, 1000.0, 0.55), gain=g)
            for pos in s.get("kick", []):
                place(t_bar + pos * beat, kick(), gain=0.9 * g)
                drums["kick"].append(t_bar + pos * beat)
            for pos in s.get("snare", []):
                place(t_bar + pos * beat, snare(rng), pan=0.1, gain=0.7 * g)
                drums["snare"].append(t_bar + pos * beat)
            for pos in s.get("rim", []):
                place(t_bar + pos * beat, snare(rng, gain=0.4), pan=-0.2, gain=g)
                drums["snare"].append(t_bar + pos * beat)
            hats = s.get("hats")
            if hats:
                steps = 8 if hats == "eighth" else 16
                for k in range(steps):
                    accent = 1.0 if k % (steps // 4) == 0 else 0.6
                    place(t_bar + k * bar / steps, hat(rng), pan=0.4, gain=accent * g)
                    drums["hat"].append(t_bar + k * bar / steps)
            if s.get("open_hat"):
                for k in range(4):
                    place(t_bar + (k + 0.5) * beat, hat(rng, open_hat=True), pan=-0.4, gain=0.8 * g)
                    drums["hat"].append(t_bar + (k + 0.5) * beat)
            if chord is not None:
                root, quality = chord
                bass = s.get("bass")
                if bass is not None:
                    positions = [k * 0.5 for k in range(8)] if bass == "eighth" else bass
                    length = 0.5 if bass == "eighth" else min(1.0, 4.0 / max(1, len(positions)))
                    for pos in positions:
                        place(t_bar + pos * beat, tone([midi_hz(36 + root)], length * beat * 0.95, harmonics=4),
                              gain=0.55 * g)
                if s.get("pad"):
                    notes = chord_notes(root, quality, 60)
                    if s.get("bright"):
                        notes += chord_notes(root, quality, 72)
                    place(t_bar, tone(notes, bar, attack=0.04, release=0.15), pan=-0.1, gain=0.6 * g)
            if s.get("crash") and b == 0:
                place(t_bar, crash(rng), gain=0.8 * g)
        if s.get("roll"):
            t0 = LEAD + bar_index * bar
            length = s["bars"] * bar
            t, step = 0.0, beat / 2
            while t < length - 1e-9:
                place(t0 + t, snare(rng, gain=0.3 + 0.5 * t / length), gain=g)
                drums["snare"].append(t0 + t)
                t += step
                step = max(beat / 8, step * 0.93)
        if s.get("riser"):
            place(LEAD + bar_index * bar, riser(rng, s["bars"] * bar), gain=g)
        bar_index += s["bars"]

    out *= 0.89 / max(1e-9, np.max(np.abs(out)))  # -1 dBFS peak
    truth = dict(
        duration=duration, tempo=float(bpm),
        beats=np.round(np.array(truth_beats), TIME_DECIMALS),
        downbeats=np.round(np.array(truth_downbeats), TIME_DECIMALS),
        drums={k: np.round(np.array(sorted(set(v))), TIME_DECIMALS) for k, v in drums.items()},
        boundaries=np.round(np.array(boundaries), TIME_DECIMALS),
        key=clip["key"],
    )
    return out, truth


def write_wav(path, stereo):
    pcm = np.clip(np.round(stereo * 32767.0), -32768, 32767).astype(np.int16)
    scipy.io.wavfile.write(path, SR, pcm)


# ---- The reference analyser (librosa) --------------------------------------------------------------------

def key_name(key):
    root, mode = key
    return f"{PITCH_NAMES[root]} {mode}"


def kk_key(mean_chroma):
    """The best of 24 keys by correlation with the Krumhansl–Kessler profiles."""
    best, best_r = (0, "major"), -np.inf
    for root in range(12):
        for mode, profile in (("major", KK_MAJOR), ("minor", KK_MINOR)):
            r = np.corrcoef(mean_chroma, np.roll(profile, root))[0, 1]
            if r > best_r:
                best, best_r = (root, mode), r
    return best


def librosa_reference(y, truth):
    import librosa

    env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=HOP, n_fft=N_FFT, aggregate=np.median)
    tempo, beat_frames = librosa.beat.beat_track(onset_envelope=env, sr=SR, hop_length=HOP, units="frames")
    tempo = float(np.atleast_1d(tempo)[0])
    beats = np.round(librosa.frames_to_time(beat_frames, sr=SR, hop_length=HOP), TIME_DECIMALS)
    # tuning=0: the clips are synthesized at A440, and the offline port does not estimate tuning (yet).
    chroma = librosa.feature.chroma_cqt(y=y, sr=SR, hop_length=HOP, tuning=0.0)
    key = kk_key(chroma.mean(axis=1))

    # Sections: beat-synchronous chroma and MFCCs, clustered in time (librosa.segment.agglomerative) into as
    # many segments as the truth has. Told the count, this is an oracle-k reference, which is what it is for:
    # an upper mark for where boundaries can land from these features, not a competitor.
    boundaries = np.array([])
    if len(truth["boundaries"]) and len(beat_frames) > len(truth["boundaries"]) + 1:
        mfcc = librosa.feature.mfcc(y=y, sr=SR, n_mfcc=13, hop_length=HOP, n_fft=N_FFT)
        width = min(chroma.shape[1], mfcc.shape[1])
        frames = librosa.util.fix_frames(beat_frames[beat_frames < width], x_min=0, x_max=width)
        feat = np.vstack([librosa.util.sync(chroma[:, :width], frames, aggregate=np.median),
                          librosa.util.sync(mfcc[:, :width], frames)])
        feat = (feat - feat.mean(axis=1, keepdims=True)) / (feat.std(axis=1, keepdims=True) + 1e-9)
        bounds = librosa.segment.agglomerative(feat, k=len(truth["boundaries"]) + 1)
        times = librosa.frames_to_time(frames[bounds], sr=SR, hop_length=HOP)
        boundaries = np.round(np.array([t for t in times if 0.0 < t < truth["duration"]]), TIME_DECIMALS)
    # Frame features for the offline analyser's second slice (#464), every one at the same hop and FFT.
    stft = librosa.stft(y, n_fft=N_FFT, hop_length=HOP)
    mag = np.abs(stft)
    harm, perc = librosa.decompose.hpss(mag)
    mean_env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=HOP, n_fft=N_FFT)
    onsets = librosa.onset.onset_detect(onset_envelope=mean_env, sr=SR, hop_length=HOP, units="time")
    columns = [
        librosa.feature.rms(y=y, frame_length=N_FFT, hop_length=HOP)[0],
        librosa.feature.spectral_centroid(S=mag, sr=SR, n_fft=N_FFT)[0],
        librosa.feature.spectral_rolloff(S=mag, sr=SR, n_fft=N_FFT)[0],
        librosa.feature.spectral_flatness(S=mag)[0],
        mean_env,
        np.sum(harm ** 2, axis=0),
        np.sum(perc ** 2, axis=0),
    ] + list(chroma)
    frames = min(len(c) for c in columns)
    features = np.stack([c[:frames] for c in columns], axis=1)
    return dict(env=env, tempo=tempo, beats=beats, key=key, boundaries=boundaries, features=features,
                onsets=np.round(onsets, TIME_DECIMALS))


def mir_eval_scores(truth, ref):
    """mir_eval's numbers for librosa's answers, under the bench's conventions (core/tests/analysis/MirEval.hpp):
    beats trimmed to t >= 5 s and matched within 70 ms; boundaries internal only (trim=True), at 0.5 and 3 s;
    the key by MIREX weighting."""
    import mir_eval

    scores = {}
    scores["beat_f"] = mir_eval.beat.f_measure(
        mir_eval.beat.trim_beats(truth["beats"]), mir_eval.beat.trim_beats(ref["beats"]), 0.07)
    if truth["key"] is not None:
        scores["key_weighted"] = mir_eval.key.weighted_score(key_name(truth["key"]), key_name(ref["key"]))
    if len(truth["boundaries"]):
        def intervals(bounds):
            edges = np.concatenate([[0.0], bounds, [truth["duration"]]])
            return np.stack([edges[:-1], edges[1:]], axis=1)
        for window in (0.5, 3.0):
            _, _, f = mir_eval.segment.detection(
                intervals(truth["boundaries"]), intervals(ref["boundaries"]), window=window, trim=True)
            scores[f"boundary_f_{window:g}"] = f
    return scores


# ---- Output ----------------------------------------------------------------------------------------------

def write_rows(path, rows):
    with open(path, "w", encoding="utf-8") as f:
        for row in rows:
            f.write("\t".join(str(v) for v in row) + "\n")


def times_rows(kind, times):
    return [(kind, f"{t:.{TIME_DECIMALS}f}") for t in times]


def read_manifest(path):
    rows = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            name, source, licence, sha = (line.rstrip("\n").split("\t") + ["", "", "", ""])[:4]
            rows[name] = dict(source=source, licence=licence, sha256=sha)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", required=True, help="output folder (created)")
    parser.add_argument("--no-librosa", action="store_true", help="clips and truth only, no reference")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    manifest = read_manifest(os.path.join(os.path.dirname(os.path.abspath(__file__)), "manifest.tsv"))
    manifest_rows = [("# clip", "source", "licence", "sha256")]
    for clip in CLIPS:
        entry = manifest.get(clip["name"])
        if entry is None or not entry["licence"]:
            sys.exit(f"{clip['name']}: no licence row in tools/analysis_lab/manifest.tsv; every clip must be redistributable")

        stereo, truth = render(clip)
        wav = os.path.join(args.out, clip["name"] + ".wav")
        write_wav(wav, stereo)
        with open(wav, "rb") as f:
            sha = hashlib.sha256(f.read()).hexdigest()
        if entry["source"] != "synthesized" and entry["sha256"] != sha:
            sys.exit(f"{clip['name']}: checksum {sha} does not match the manifest's {entry['sha256']}")
        manifest_rows.append((clip["name"], entry["source"], entry["licence"], sha))

        rows = [("duration", f"{truth['duration']:.{TIME_DECIMALS}f}"), ("tempo", f"{truth['tempo']:g}")]
        if truth["key"] is not None:
            rows.append(("key", truth["key"][0], truth["key"][1]))
        rows += times_rows("beat", truth["beats"]) + times_rows("downbeat", truth["downbeats"])
        rows += times_rows("boundary", truth["boundaries"])
        for kind in ("kick", "snare", "hat"):
            rows += times_rows(kind, truth["drums"][kind])
        write_rows(os.path.join(args.out, clip["name"] + ".truth.tsv"), rows)

        if args.no_librosa:
            continue
        import librosa

        # librosa.load folds to mono as the mean of the channels, the same (L+R)/2 the C++ side uses, and
        # reads PCM16 as s/32768.
        y, sr = librosa.load(wav, sr=None, mono=True)
        assert sr == SR
        ref = librosa_reference(y, truth)
        rows = [("tempo", f"{ref['tempo']:.6f}"), ("key", ref["key"][0], ref["key"][1])]
        rows += times_rows("beat", ref["beats"]) + times_rows("boundary", ref["boundaries"])
        write_rows(os.path.join(args.out, clip["name"] + ".librosa.tsv"), rows)
        write_rows(os.path.join(args.out, clip["name"] + ".onset.tsv"), [(f"{v:.9g}",) for v in ref["env"]])
        write_rows(os.path.join(args.out, clip["name"] + ".features.tsv"),
                   [("# rms", "centroid_hz", "rolloff_hz", "flatness", "onset_mean", "harmonic", "percussive")
                    + tuple(f"chroma_{p}" for p in PITCH_NAMES)]
                   + [tuple(f"{v:.9g}" for v in row) for row in ref["features"]])
        write_rows(os.path.join(args.out, clip["name"] + ".onsets.tsv"), times_rows("onset", ref["onsets"]))
        write_rows(os.path.join(args.out, clip["name"] + ".mir_eval.tsv"),
                   [(k, f"{v:.9f}") for k, v in mir_eval_scores(truth, ref).items()])
        print(f"{clip['name']}: librosa tempo {ref['tempo']:.2f} (truth {truth['tempo']:g}), "
              f"{len(ref['beats'])} beats, key {key_name(ref['key'])}")

    write_rows(os.path.join(args.out, "manifest.tsv"), manifest_rows)
    write_rows(os.path.join(args.out, "clips.txt"), [(c["name"],) for c in CLIPS])
    versions = [("python", sys.version.split()[0]), ("numpy", np.__version__)]
    if not args.no_librosa:
        import librosa
        import mir_eval
        versions += [("librosa", librosa.__version__), ("mir_eval", mir_eval.__version__)]
    write_rows(os.path.join(args.out, "versions.tsv"), versions)


if __name__ == "__main__":
    main()
