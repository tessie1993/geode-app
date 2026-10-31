"""Turns a music file into the sound engine's frames, for glslcheck to render a style against.

    python track.py <audio> <out.json> [--from S] [--seconds N] [--fps F]

core/sound runs on the device (there is no host C++ toolchain on every machine), so this is a numpy
stand-in for core/sound/SoundEngine, written from the definitions in core/sound/SoundFrame.h: relative
bands over their own ~4 s mean, fast att envelopes, presence and intensity, the music clocks, kick /
snare / hat triggers with envelopes and counted hits, onsets, hard cuts, surges, the lead pitch. The
numbers are the same kind and range as the engine's, not the same to the decimal, which is what a
look at a style needs: does it move on the kick, breathe with the bass, change at the drop.

Output: {"fps", "from", "frames": [{"SND_BASS": ..., ..., "counts": [onsets, cuts], "travel": z}]},
one frame per rendered frame. check.mjs --track reads it.
"""
import json
import math
import subprocess
import sys

import numpy as np

SR = 44100
WIN = 2048


def decode(path, start, seconds):
    # Pre-roll so the running means have settled by the first frame we keep.
    pre = min(start, 8.0)
    cmd = ["ffmpeg", "-v", "error", "-ss", str(start - pre), "-t", str(seconds + pre), "-i", path,
           "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32), pre


def ema(prev, x, dt, tau):
    return prev + (x - prev) * (1.0 - math.exp(-dt / tau))


def env(prev, x, dt, up, down):
    return ema(prev, x, dt, up if x > prev else down)


def bend(x):
    """Relative level bent smoothly so nothing passes 4 (the engine's soft ceiling)."""
    return 4.0 * math.tanh(x / 4.0)


def main():
    args = sys.argv[1:]
    src, out = args[0], args[1]
    start = float(args[args.index("--from") + 1]) if "--from" in args else 60.0
    seconds = float(args[args.index("--seconds") + 1]) if "--seconds" in args else 8.0
    fps = float(args[args.index("--fps") + 1]) if "--fps" in args else 24.0

    pcm, pre = decode(src, start, seconds)
    dt = 1.0 / fps
    hop = SR / fps
    freqs = np.fft.rfftfreq(WIN, 1.0 / SR)
    window = np.hanning(WIN).astype(np.float32)
    tilt = np.maximum(freqs, 20.0) / 1000.0  # +6 dB/octave: power times frequency

    def band(lo, hi):
        return (freqs >= lo) & (freqs < hi)

    bands = {"bass": band(20, 250), "mid": band(250, 4000), "treble": band(4000, 16000),
             "sub": band(20, 60), "midHigh": band(2000, 6000)}
    kickBand, snareBand, hatBand = band(40, 130), band(1500, 5000), band(7000, 14000)
    pitchBand = band(80, 1500)
    logf = np.log(np.maximum(freqs, 20.0) / 20.0) / math.log(1000.0)

    s = {k: 1.0 for k in ["bassMean", "midMean", "trebleMean", "subMean", "midHighMean"]}
    s.update(bassAtt=1.0, midAtt=1.0, trebleAtt=1.0, subAtt=1.0, midHighAtt=1.0)
    for k in ["low", "middle", "high"]:
        s[k] = 0.5
    for k in ["bassPresence", "midPresence", "treblePresence"]:
        s[k] = 0.5
    s.update(intensity=0.3, presence=1.0, clocks=[0.0] * 6, loudShort=0.5, loudLong=0.5,
             kickEnv=0, snareEnv=0, hatEnv=0, onsetEnv=0, kickToggle=0, snareToggle=0, kickRandom=0.5,
             noteRandom=0.5, kicks=0, snares=0, onsets=0, cuts=0, heldCut=0, heldSurge=0, heldReturned=0,
             heldNote=0, sinceCut=99.0, sinceKick=9, sinceSnare=9, sinceHat=9, prevSpec=None,
             kickMean=1e-6, snareMean=1e-6, hatMean=1e-6, fluxMean=1e-6, pitchClass=0.0, pitch=0.0,
             noteStable=0, lastClass=-1, travel=37.0, silentFor=0.0, hitEase=0.0, noteEase=0.0)
    rng = np.random.default_rng(7)
    frames = []
    total = int((pre + seconds) * fps)
    for i in range(total):
        centre = int(i * hop)
        seg = pcm[max(centre - WIN // 2, 0): centre + WIN // 2]
        if len(seg) < WIN:
            seg = np.pad(seg, (0, WIN - len(seg)))
        spec = np.abs(np.fft.rfft(seg * window)) ** 2
        tilted = spec * tilt
        rms = float(np.sqrt(np.mean(seg ** 2)) + 1e-9)
        loud = min(max((20 * math.log10(rms) + 60) / 60, 0.0), 1.0)
        f = {}
        for name, m in bands.items():
            e = float(tilted[m].sum()) + 1e-12
            s[name + "Mean"] = ema(s[name + "Mean"], e, dt, 4.0)
            rel = bend(e / max(s[name + "Mean"], 1e-12))
            s[name + "Att"] = env(s[name + "Att"], rel, dt, 0.02, 0.05)
            f[name] = rel
        vol = (f["bass"] + f["mid"] + f["treble"]) / 3
        volAtt = (s["bassAtt"] + s["midAtt"] + s["trebleAtt"]) / 3
        for k, a in (("low", "bassAtt"), ("middle", "midAtt"), ("high", "trebleAtt")):
            s[k] = env(s[k], min(max(s[a] * 0.5, 0.0), 1.0), dt, 0.03, 0.15)
        for k, a in (("bassPresence", "bassAtt"), ("midPresence", "midAtt"), ("treblePresence", "trebleAtt")):
            s[k] = ema(s[k], min(s[a] / 1.5, 1.0), dt, 3.0)
        s["loudShort"] = ema(s["loudShort"], loud, dt, 0.4)
        s["loudLong"] = ema(s["loudLong"], loud, dt, 8.0)
        trend = 0.5 * s["loudShort"] / max(s["loudLong"], 1e-3)
        s["intensity"] = min(max(s["intensity"] + dt * (0.05 if trend > 0.5 else -0.03), 0.0), 1.0)
        playing = rms > 10 ** (-50 / 20)
        s["silentFor"] = 0.0 if playing else s["silentFor"] + dt
        s["presence"] = min(max(s["presence"] + dt * (0.5 if playing else -0.5), 0.0), 1.0)

        # Drums: each band's positive change over its own mean, with a minimum gap.
        def onset(m, meanKey, sinceKey, gap, thresh):
            prev = s["prevSpec"]
            e = float(np.maximum(spec[m] - (prev[m] if prev is not None else spec[m]), 0).sum())
            s[meanKey] = ema(s[meanKey], e, dt, 1.5)
            s[sinceKey] += dt
            strength = e / max(s[meanKey], 1e-12)
            if strength > thresh and s[sinceKey] > gap:
                s[sinceKey] = 0.0
                return min(strength / (thresh * 2), 1.0)
            return 0.0

        kick = onset(kickBand, "kickMean", "sinceKick", 0.2, 2.2)
        snare = onset(snareBand, "snareMean", "sinceSnare", 0.15, 2.4)
        hat = onset(hatBand, "hatMean", "sinceHat", 0.08, 2.0)
        prev = s["prevSpec"]
        flux = float(np.maximum(tilted - (prev * tilt if prev is not None else tilted), 0).sum())
        s["fluxMean"] = ema(s["fluxMean"], flux, dt, 2.0)
        fluxRel = bend(flux / max(s["fluxMean"], 1e-12))
        on = 1.0 if fluxRel > 2.0 and (kick or snare or hat) else 0.0
        s["prevSpec"] = spec
        trig = {"kick": kick, "snare": snare, "hat": hat, "onset": on}
        for k, d in (("kickEnv", 0.18), ("snareEnv", 0.15), ("hatEnv", 0.08), ("onsetEnv", 0.2)):
            s[k] = max(s[k] * math.exp(-dt / d), trig[k.replace("Env", "")])
        if kick:
            s["kicks"] += 1
            s["kickToggle"] = 1 - s["kickToggle"]
            s["kickRandom"] = float(rng.random())
            s["hitEase"] += 1
        if snare:
            s["snares"] += 1
            s["snareToggle"] = 1 - s["snareToggle"]
            s["hitEase"] += 1
        if on:
            s["onsets"] += 1
        counted = {f"kick{n}": kick if kick and s["kicks"] % n == 0 else 0.0 for n in (2, 4, 8, 16)}
        counted.update({f"snare{n}": snare if snare and s["snares"] % n == 0 else 0.0 for n in (2, 4)})

        # Big moments: the short loudness jumping well over the long one, at most every 15 s.
        s["sinceCut"] += dt
        hardCut = 0.0
        if trend > 0.6 and s["sinceCut"] > 15.0 and i > fps * 2:
            hardCut, s["sinceCut"] = 1.0, 0.0
            s["cuts"] += 1
        surge = 1.0 if hardCut and s["loudLong"] < 0.6 else 0.0
        returned = 1.0 if playing and s["silentFor"] == 0 and frames and frames[-1]["SND_SILENCE"] > 0 else 0.0
        silence = 1.0 if (not playing) and abs(s["silentFor"] - dt) < 1e-9 else 0.0
        for k, trigv, d in (("heldCut", hardCut, 1.6), ("heldSurge", surge, 1.6), ("heldReturned", returned, 1.6)):
            s[k] = max(s[k] - dt / d, trigv, 0.0)

        # The lead pitch: the strongest bin between 80 Hz and 1.5 kHz, if it stands out.
        pm = spec * pitchBand
        k = int(np.argmax(pm))
        conf = float(pm[k] / (spec[pitchBand].mean() * 40 + 1e-12))
        noteChange = 0.0
        if conf > 0.3 and freqs[k] > 0:
            midi = 69 + 12 * math.log2(freqs[k] / 440.0)
            cls = int(round(midi)) % 12
            if cls != s["lastClass"]:
                s["noteStable"] += 1
                if s["noteStable"] >= 2:
                    s["lastClass"], s["noteStable"], noteChange = cls, 0, 1.0
                    s["noteRandom"] = float(rng.random())
                    s["noteEase"] += 1
            s["pitch"], s["pitchClass"] = midi, s["lastClass"] / 12.0 if s["lastClass"] >= 0 else 0.0
        s["heldNote"] = max(s["heldNote"] - dt / 0.8, noteChange, 0.0)

        c = s["clocks"]
        for j, rate in enumerate([s["bassAtt"], s["midAtt"], s["trebleAtt"], volAtt ** 2.5]):
            c[j] += dt * rate * s["presence"]
        c[4] = ema(c[4], s["hitEase"], dt, 0.15)
        c[5] = ema(c[5], s["noteEase"], dt, 0.15)
        # The rig's travel: fly speed driven by the curved clock's rate (matrix::drive), gated by presence.
        s["travel"] += dt * 1.6 * min(2.0 * volAtt ** 1.2, 2.5) * s["presence"]

        cen = float((spec * logf).sum() / (spec.sum() + 1e-12))
        flat = float(np.exp(np.mean(np.log(spec[1:] + 1e-12))) / (spec[1:].mean() + 1e-12))
        flatness = min(max((10 * math.log10(flat + 1e-12) + 30) / 30, 0.0), 1.0)
        if i < pre * fps:
            continue
        frame = {
            "SND_BASS": f["bass"], "SND_MID": f["mid"], "SND_TREBLE": f["treble"], "SND_VOL": vol,
            "SND_BASS_ATT": s["bassAtt"], "SND_MID_ATT": s["midAtt"], "SND_TREBLE_ATT": s["trebleAtt"],
            "SND_VOL_ATT": volAtt, "SND_SUB": f["sub"], "SND_SUB_ATT": s["subAtt"], "SND_MID_HIGH": f["midHigh"],
            "SND_MID_HIGH_ATT": s["midHighAtt"], "SND_LOW": s["low"], "SND_MIDDLE": s["middle"], "SND_HIGH": s["high"],
            "SND_LEVEL": loud, "SND_BASS_PRESENCE": s["bassPresence"], "SND_MID_PRESENCE": s["midPresence"],
            "SND_TREBLE_PRESENCE": s["treblePresence"], "SND_INTENSITY": s["intensity"],
            "SND_BASS_TIME": c[0], "SND_MID_TIME": c[1], "SND_TREBLE_TIME": c[2], "SND_CURVED_TIME": c[3],
            "SND_HIT_TIME": c[4], "SND_NOTE_TIME": c[5], "SND_PRESENCE": s["presence"], "SND_RMS": rms,
            "SND_LOUDNESS": loud, "SND_CENTROID": cen, "SND_ROLLOFF": min(cen + 0.15, 1.0), "SND_FLATNESS": flatness,
            "SND_DENSITY_SLOW": 0.5, "SND_DENSITY_FAST": 0.5, "SND_FLUX": fluxRel, "SND_ENERGY_TREND": trend,
            "SND_PEAK_FREQ": float(logf[k]), "SND_PEAK_MAG": min(conf, 1.0), "SND_STEREO_WIDTH": 0.3,
            "SND_STEREO_PAN": 0.0, "SND_KICK_ENV": s["kickEnv"], "SND_SNARE_ENV": s["snareEnv"],
            "SND_HAT_ENV": s["hatEnv"], "SND_ONSET_ENV": s["onsetEnv"], "SND_KICK_TOGGLE": s["kickToggle"],
            "SND_SNARE_TOGGLE": s["snareToggle"], "SND_KICK_RANDOM": s["kickRandom"],
            "SND_NOTE_RANDOM": s["noteRandom"], "SND_KICK": kick, "SND_SNARE": snare, "SND_HAT": hat,
            "SND_KICK2": counted["kick2"], "SND_KICK4": counted["kick4"], "SND_KICK8": counted["kick8"],
            "SND_KICK16": counted["kick16"], "SND_SNARE2": counted["snare2"], "SND_SNARE4": counted["snare4"],
            "SND_HARD_CUT": hardCut, "SND_ONSET": on, "SND_SILENCE": silence, "SND_RETURNED": returned,
            "SND_SURGE": surge, "SND_NOTE_CHANGE": noteChange, "SND_LEGATO": 0.0, "SND_REATTACK": 0.0,
            "SND_PITCH": s["pitch"], "SND_PITCH_CLASS": s["pitchClass"],
            "SND_PITCH_HEIGHT": min(max((s["pitch"] - 36) / 48, 0.0), 1.0), "SND_PITCH_CONFIDENCE": min(conf, 1.0),
            "SND_INTERVAL": 0.0, "SND_GLIDE": 0.0, "SND_VIBRATO": 0.0, "SND_EVENNESS": 0.5, "SND_RUN_DENSITY": 0.3,
            "SND_HELD_HARD_CUT": s["heldCut"], "SND_HELD_SURGE": s["heldSurge"],
            "SND_HELD_RETURNED": s["heldReturned"], "SND_HELD_NOTE_CHANGE": s["heldNote"],
            "counts": [s["onsets"], s["cuts"]], "travel": s["travel"],
        }
        frames.append({k: (round(v, 5) if isinstance(v, float) else v) for k, v in frame.items()})
    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"fps": fps, "from": start, "frames": frames}, fh)
    kicks = sum(1 for fr in frames if fr["SND_KICK"] > 0)
    print(f"{len(frames)} frames at {fps} fps from {start}s: {kicks} kicks, {frames[-1]['counts'][1]} cuts")


if __name__ == "__main__":
    main()
