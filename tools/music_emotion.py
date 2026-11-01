#!/usr/bin/env python3
"""The emotion half of the musical intelligence database: what each scale (mode) and each key feels like.

Writes, from the SCALES table in musical_intelligence.py and the KEYS table below:
  core/sound/EmotionTable.inc            the engine's copy: a mode vector per scale, a flavour per key
  docs/musical-intelligence/emotions.csv every key x scale fingerprint (12 x 46 = 552 rows), for reading
  docs/musical-intelligence/key-characters.csv  the historical key characters and their codes

Two kinds of evidence, kept apart on purpose:
  * a scale's emotion is measured: the third decides most of happy/sad (Eerola, Friberg & Bresin 2013;
    Temperley & Tan 2013), each lowered degree darkens it (Parncutt 2014), semitones, tritones, augmented
    seconds and symmetric scales add tension (Poudrier et al. 2024). The structural formulas below cover
    all 46 scales from their notes; IDIOMS adds the character of the scales with a known one.
  * a key's character is tradition (Schubart 1806 in Steblin's translation, checked against Mattheson 1713
    and Charpentier c. 1692): listeners cannot hear it in equal temperament (Powell & Dibben 2005). It
    steers the kind of feeling (GEMS) and the warmth, never the valence, and it weighs a quarter.

The feeling is GEMS-9 (Zentner, Grandjean & Scherer 2008), the music-specific emotion scale, beside three
axes: valence, energy (arousal) and tension.

Run: python3 tools/music_emotion.py   (musical_intelligence.py runs it too)
"""
import csv
import os

from musical_intelligence import SCALES, SHARP

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "docs", "musical-intelligence")
INC = os.path.join(HERE, "..", "core", "sound", "EmotionTable.inc")

GEMS = ["wonder", "transcendence", "tenderness", "nostalgia", "peace", "power", "joy", "tension", "sadness"]
MODE_WEIGHT = 0.75  # a scale's measured emotion against the key's tradition

# Scales with a known character (theory and convention; evidence in the research doc). Blended half and
# half with the structural guess.
IDIOMS = {
    "ionian": {"joy": 0.6, "peace": 0.4},
    "lydian": {"wonder": 0.7, "transcendence": 0.4, "joy": 0.2},
    "mixolydian": {"joy": 0.5, "power": 0.4},
    "dorian": {"nostalgia": 0.5, "tenderness": 0.4, "joy": 0.1},
    "aeolian": {"sadness": 0.6, "nostalgia": 0.4},
    "phrygian": {"tension": 0.5, "sadness": 0.4, "power": 0.2},
    "locrian": {"tension": 0.8, "sadness": 0.3},
    "harmonic_minor": {"tension": 0.5, "power": 0.4, "sadness": 0.3},
    "phrygian_dominant": {"power": 0.5, "tension": 0.4, "wonder": 0.3},
    "double_harmonic": {"power": 0.4, "tension": 0.4, "wonder": 0.4},
    "hungarian_minor": {"tension": 0.5, "wonder": 0.3, "sadness": 0.3},
    "whole_tone": {"wonder": 0.6, "transcendence": 0.5},
    "augmented": {"wonder": 0.5, "tension": 0.4},
    "diminished_hw": {"tension": 0.6, "wonder": 0.3},
    "diminished_wh": {"tension": 0.6, "wonder": 0.3},
    "major_pentatonic": {"peace": 0.5, "joy": 0.5},
    "suspended_pentatonic": {"peace": 0.5, "transcendence": 0.3},
    "minor_pentatonic": {"power": 0.4, "nostalgia": 0.4, "joy": 0.2},
    "blues_minor_pentatonic": {"nostalgia": 0.4, "tenderness": 0.3, "peace": 0.2},
    "blues_major_pentatonic": {"joy": 0.4, "peace": 0.4},
    "blues": {"power": 0.4, "nostalgia": 0.4, "joy": 0.2},
    "major_blues": {"joy": 0.5, "nostalgia": 0.3},
    "hirajoshi": {"peace": 0.4, "sadness": 0.4, "nostalgia": 0.2},
    "in_sen": {"sadness": 0.4, "peace": 0.3, "tension": 0.2},
    "iwato": {"tension": 0.5, "sadness": 0.3, "wonder": 0.2},
    "lydian_dominant": {"wonder": 0.5, "joy": 0.3, "tension": 0.2},
    "lydian_augmented": {"wonder": 0.6, "transcendence": 0.3, "tension": 0.2},
    "melodic_minor": {"nostalgia": 0.4, "tenderness": 0.3, "wonder": 0.2},
    "altered": {"tension": 0.7, "power": 0.2},
    "chromatic": {"tension": 0.8},
    "enigmatic": {"wonder": 0.5, "tension": 0.5},
    "prometheus": {"wonder": 0.6, "transcendence": 0.4},
    "persian": {"tension": 0.5, "power": 0.3, "wonder": 0.3},
    "neapolitan_minor": {"sadness": 0.5, "tension": 0.4},
}

# The historical key characters. Tonic, third ("maj"/"min"), signature (sharps +, flats -), Schubart's
# words (Steblin's translation, condensed), what Mattheson/Charpentier said where they differ, the GEMS code.
# `agree` is how far the three sources agree, 0..1: a contested key's flavour is shrunk toward neutral.
KEYS = [
    (0, "maj", 0, "innocence, simplicity, naivety", "Mattheson: rude, bold; Charpentier: gay, warlike", 0.6,
     {"peace": 0.5, "joy": 0.4, "tenderness": 0.3}),
    (1, "maj", -5, "a leer degenerating into grief and rapture; it can smile but not laugh", "", 0.8,
     {"nostalgia": 0.4, "tension": 0.3, "sadness": 0.2, "wonder": 0.2}),
    (2, "maj", 2, "triumph, hallelujahs, war cries, victory rejoicing", "all three: joyful, martial", 1.0,
     {"power": 0.6, "joy": 0.5}),
    (3, "maj", -3, "love, devotion, intimate conversation with God", "Charpentier: cruel, hard", 0.5,
     {"tenderness": 0.5, "transcendence": 0.5}),
    (4, "maj", 4, "noisy shouts of joy, laughing pleasure, not yet full delight", "Mattheson: fatal sadness; Charpentier: quarrelsome", 0.4,
     {"joy": 0.7, "power": 0.3}),
    (5, "maj", -1, "complaisance and calm", "Mattheson: generous love; Charpentier: furious", 0.5,
     {"peace": 0.6, "tenderness": 0.3}),
    (6, "maj", 6, "triumph over difficulty, the relieved sigh after the struggle", "", 0.8,
     {"power": 0.4, "joy": 0.3, "transcendence": 0.3}),
    (7, "maj", 1, "rustic, idyllic, lyrical; calm and satisfied passion, gratitude", "all three: gentle, quietly joyful", 1.0,
     {"peace": 0.5, "tenderness": 0.4, "joy": 0.3}),
    (8, "maj", -4, "death, the grave, putrefaction, judgement, eternity", "", 0.8,
     {"transcendence": 0.5, "sadness": 0.4, "tension": 0.2}),
    (9, "maj", 3, "innocent love, contentment, youthful cheer, trust in God", "Charpentier: joyful, pastoral", 0.9,
     {"joy": 0.5, "tenderness": 0.4}),
    (10, "maj", -2, "cheerful love, clear conscience, hope, aspiration", "Mattheson and Charpentier: magnificent", 0.8,
     {"joy": 0.4, "tenderness": 0.3, "wonder": 0.3}),
    (11, "maj", 5, "glaring colours, wild passions: anger, rage, jealousy, despair", "Charpentier: harsh, plaintive", 0.8,
     {"power": 0.5, "tension": 0.5}),
    (0, "min", -3, "declaration of love and lament of unhappy love; languishing, longing", "Charpentier: obscure, sad", 0.8,
     {"nostalgia": 0.5, "sadness": 0.4, "tenderness": 0.3}),
    (1, "min", 4, "penitence, intimate conversation with God, sighs of disappointed love", "", 0.8,
     {"transcendence": 0.4, "sadness": 0.4}),
    (2, "min", -1, "melancholy womanliness, brooding spleen", "Mattheson: devout, calm, grand; Charpentier: grave, devout", 0.5,
     {"sadness": 0.4, "nostalgia": 0.4, "tension": 0.2}),
    (3, "min", 6, "deepest anxiety, brooding despair, the darkest key", "Charpentier (E-flat minor): horrible, frightful", 1.0,
     {"tension": 0.6, "sadness": 0.5}),
    (4, "min", 1, "naive, womanly, innocent declaration of love; lament without grumbling", "Charpentier: amorous, plaintive", 0.9,
     {"tenderness": 0.5, "nostalgia": 0.4, "sadness": 0.2}),
    (5, "min", -4, "deep depression, funeral lament, longing for the grave", "Charpentier: obscure, plaintive", 1.0,
     {"sadness": 0.8, "nostalgia": 0.2}),
    (6, "min", 3, "a gloomy key; resentment and discontent", "", 0.8,
     {"tension": 0.5, "sadness": 0.4}),
    (7, "min", -2, "discontent, uneasiness, resentful gnawing", "Mattheson: graceful; Charpentier: serious, magnificent", 0.4,
     {"tension": 0.4, "sadness": 0.3, "power": 0.2}),
    (8, "min", 5, "a grumbler, the suffocated heart, wailing struggle", "", 0.8,
     {"tension": 0.5, "sadness": 0.5}),
    (9, "min", 0, "pious womanliness and tenderness of character", "Mattheson: plaintive, dignified; Charpentier: tender, plaintive", 1.0,
     {"tenderness": 0.5, "nostalgia": 0.3, "sadness": 0.2}),
    (10, "min", -5, "a surly creature, mocking God and the world; the garment of night", "Charpentier: obscure, terrible", 0.9,
     {"tension": 0.4, "power": 0.3, "sadness": 0.3}),
    (11, "min", 2, "patience, calm awaiting of fate, submission", "Mattheson: bizarre, melancholy; Charpentier: solitary, melancholy", 0.6,
     {"peace": 0.4, "nostalgia": 0.4, "sadness": 0.3}),
]


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def has(mask, pc):
    return (mask >> (pc % 12)) & 1 == 1


def rot(mask, n):
    return ((mask >> n) | (mask << (12 - n))) & 0xFFF


def features(st):
    mask = sum(1 << s for s in st)
    pcs = sorted(st)
    major3, minor3 = has(mask, 4), has(mask, 3)
    third = 1.0 if major3 and not minor3 else (-1.0 if minor3 and not major3 else (0.3 if major3 else 0.0))
    # Lowered against Ionian: a flat degree sounding without its natural.
    lowered = sum(1 for flat, nat in ((1, 2), (3, 4), (8, 9), (10, 11)) if has(mask, flat) and not has(mask, nat))
    flat2 = has(mask, 1) and not has(mask, 2)
    flat5 = has(mask, 6) and not has(mask, 7)
    sharp4 = has(mask, 6) and has(mask, 7) and not has(mask, 5)
    steps = [(pcs[(i + 1) % len(pcs)] - pcs[i]) % 12 for i in range(len(pcs))]
    aug2 = sum(1 for s in steps if s == 3) if len(pcs) >= 7 else 0
    ic = [0] * 7
    for i in range(len(pcs)):
        for j in range(i + 1, len(pcs)):
            d = (pcs[j] - pcs[i]) % 12
            ic[min(d, 12 - d)] += 1
    symmetric = any(rot(mask, n) == mask for n in (2, 3, 4)) and len(pcs) < 12
    pentatonic = len(pcs) == 5 and ic[1] == 0
    return dict(mask=mask, third=third, lowered=lowered, flat2=flat2, flat5=flat5, sharp4=sharp4, aug2=aug2,
                semitones=ic[1], tritones=ic[6], symmetric=symmetric, pentatonic=pentatonic,
                fifth=has(mask, 7), notes=len(pcs))


def normalised(g):
    s = sum(max(0.0, v) for v in g.values())
    return {k: (max(0.0, g.get(k, 0.0)) / s if s > 0 else 1.0 / len(GEMS)) for k in GEMS}


def mode_vector(sid, st, bright, tension):
    f = features(st)
    v = 0.35 * f["third"] - 0.1 * f["lowered"] - 0.15 * f["flat2"] - 0.2 * f["flat5"] + 0.1 * f["pentatonic"]
    v = clamp(0.6 * v + 0.4 * bright, -1.0, 1.0)
    t = (0.06 * f["semitones"] + 0.12 * f["tritones"] + 0.15 * f["aug2"] + 0.3 * f["symmetric"]
         + 0.3 * (not f["fifth"]))
    t = clamp(0.5 * clamp(t, 0.0, 1.0) + 0.5 * tension, 0.0, 1.0)
    a = clamp(0.5 + 0.08 * f["aug2"] + 0.05 * (f["third"] > 0) - 0.08 * f["pentatonic"], 0.0, 1.0)
    pos, neg = max(v, 0.0), max(-v, 0.0)
    g = {
        "wonder": 0.5 * f["sharp4"] + 0.4 * f["symmetric"] + 0.1,
        "transcendence": 0.3 * f["symmetric"] + 0.2 * f["pentatonic"] + 0.05,
        "tenderness": 0.3 * (1 - t) * (1 - neg),
        "nostalgia": 0.4 * neg * (1 - t) + 0.1,
        "peace": 0.5 * (1 - t) * clamp(v + 0.3, 0.0, 1.0),
        "power": 0.4 * t + 0.15 * f["aug2"],
        "joy": 0.8 * pos,
        "tension": t,
        "sadness": 0.8 * neg,
    }
    g = normalised(g)
    if sid in IDIOMS:
        idiom = normalised(IDIOMS[sid])
        g = normalised({k: 0.5 * g[k] + 0.5 * idiom[k] for k in GEMS})
    return v, a, t, g, f


def key_flavour(root, quality):
    for k in KEYS:
        if k[0] == root and k[1] == quality:
            tonic, _q, sig, words, others, agree, code = k
            g = normalised(code)
            neutral = 1.0 / len(GEMS)
            g = {n: agree * g[n] + (1 - agree) * neutral for n in GEMS}
            return sig / 6.0, g, words, others, agree
    raise KeyError((root, quality))


def main():
    os.makedirs(OUT, exist_ok=True)
    modes = []
    for sid, name, _family, st, bright, tension, *_ in SCALES:
        v, a, t, g, f = mode_vector(sid, st, bright, tension)
        modes.append((sid, name, v, a, t, g, f))
    with open(INC, "w") as out:
        out.write("// Generated by tools/music_emotion.py. Do not edit: edit the script.\n")
        out.write("// Modes, in scales.csv order: {valence -1..1, energy 0..1, tension 0..1, major third,\n")
        out.write("//   GEMS-9: wonder, transcendence, tenderness, nostalgia, peace, power, joy, tension, sadness}.\n")
        out.write("#ifdef GEODE_EMOTION_MODES\n")
        for sid, _n, v, a, t, g, f in modes:
            gems = ", ".join(f"{g[k]:.3f}f" for k in GEMS)
            out.write(f"{{{v:.3f}f, {a:.3f}f, {t:.3f}f, {'true' if f['third'] > 0 else 'false'}, {{{gems}}}}},  // {sid}\n")
        out.write("#endif\n")
        out.write("// Keys: [0..11] major on C..B, [12..23] minor on C..B: {sharpness -1 (six flats) .. 1 (six sharps), GEMS-9}.\n")
        out.write("#ifdef GEODE_EMOTION_KEYS\n")
        for quality in ("maj", "min"):
            for root in range(12):
                s, g, *_ = key_flavour(root, quality)
                gems = ", ".join(f"{g[k]:.3f}f" for k in GEMS)
                out.write(f"{{{s:.3f}f, {{{gems}}}}},  // {SHARP[root]} {quality}\n")
        out.write("#endif\n")
    with open(os.path.join(OUT, "key-characters.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["key", "signature", "schubart", "mattheson_charpentier", "agreement"] + GEMS)
        for tonic, quality, sig, words, others, agree, _code in KEYS:
            _s, g, *_ = key_flavour(tonic, quality)
            w.writerow([f"{SHARP[tonic]} {'major' if quality == 'maj' else 'minor'}", sig, words, others, agree]
                       + [f"{g[k]:.3f}" for k in GEMS])
    with open(os.path.join(OUT, "emotions.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["key", "scale", "valence", "energy", "tension", "sharpness", "strongest", "second"] + GEMS)
        for sid, name, v, a, t, g, f in modes:
            quality = "maj" if f["third"] > 0 else "min"
            for root in range(12):
                s, kg, *_ = key_flavour(root, quality)
                mixed = normalised({k: MODE_WEIGHT * g[k] + (1 - MODE_WEIGHT) * kg[k] for k in GEMS})
                order = sorted(GEMS, key=lambda k: -mixed[k])
                w.writerow([SHARP[root], sid, f"{v:.3f}", f"{a:.3f}", f"{t:.3f}", f"{s:.3f}", order[0], order[1]]
                           + [f"{mixed[k]:.3f}" for k in GEMS])


if __name__ == "__main__":
    main()
