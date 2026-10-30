#!/usr/bin/env python3
"""Synthesise the black-glass theme's interface sounds into app/src/main/res/raw/.

Run from the repo root: python3 tools/ui_sounds.py
Pure standard library, deterministic output (seeded noise), 48 kHz mono 16-bit WAV.

- ui_tap:     a 40 ms glass tick: two inharmonic partials with a very fast decay.
- ui_confirm: two rising notes (E6 -> B6) with glassy partials, 260 ms.
- ui_swoop:   220 ms of soft noise through a band-pass that sweeps up, with a gentle envelope.
"""

import math
import os
import random
import struct
import wave

RATE = 48_000
OUT = os.path.join(os.path.dirname(__file__), "..", "app", "src", "main", "res", "raw")


def write(name, samples, peak_db=-14.0):
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = (10 ** (peak_db / 20.0)) / peak
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * gain)) * 32767)) for s in samples)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)


def fade_edges(samples, ms=2.0):
    n = int(RATE * ms / 1000)
    for i in range(min(n, len(samples))):
        g = i / n
        samples[i] *= g
        samples[-1 - i] *= g
    return samples


def glass(freq, t, decay):
    # A struck glass rings at roughly 1 : 2.32 : 4.25 of its fundamental.
    return (
        math.sin(2 * math.pi * freq * t) * 1.0
        + math.sin(2 * math.pi * freq * 2.32 * t) * 0.35 * math.exp(-t * decay * 1.8)
        + math.sin(2 * math.pi * freq * 4.25 * t) * 0.12 * math.exp(-t * decay * 3.0)
    ) * math.exp(-t * decay)


def tap():
    n = int(RATE * 0.040)
    return fade_edges([glass(2_400.0, i / RATE, 140.0) for i in range(n)])


def confirm():
    n = int(RATE * 0.260)
    out = []
    second = 0.070
    for i in range(n):
        t = i / RATE
        s = glass(1_318.5, t, 22.0) * 0.8
        if t >= second:
            s += glass(1_975.5, t - second, 18.0)
        out.append(s)
    return fade_edges(out)


def swoop():
    rnd = random.Random(7)
    n = int(RATE * 0.220)
    out = []
    # Two-pole resonant band-pass whose centre sweeps 600 Hz -> 3.2 kHz.
    y1 = y2 = 0.0
    for i in range(n):
        p = i / n
        fc = 600.0 * (3_200.0 / 600.0) ** p
        r = 0.985
        a1 = -2 * r * math.cos(2 * math.pi * fc / RATE)
        a2 = r * r
        x = rnd.uniform(-1.0, 1.0)
        y = x * (1 - r) - a1 * y1 - a2 * y2
        y2, y1 = y1, y
        env = math.sin(math.pi * p) ** 1.5
        out.append(y * env)
    return fade_edges(out)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    write("ui_tap", tap(), peak_db=-12.0)
    write("ui_confirm", confirm(), peak_db=-12.0)
    write("ui_swoop", swoop(), peak_db=-16.0)
