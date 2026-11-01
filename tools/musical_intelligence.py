#!/usr/bin/env python3
"""The musical intelligence database, one source for the docs and the engine. Writes
docs/musical-intelligence/*.csv (every 12-TET note from 20 Hz to 20 kHz, the scale and mode catalogue, every
scale spelled on all twelve roots) and core/sound/ScaleTable.inc, the same catalogue as C++ for the sound
engine's scale detector (core/sound/Harmony). Edit SCALES here, then run: python3 tools/musical_intelligence.py"""
import csv
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "musical-intelligence")
SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLAT = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]
INTERVAL = ["1", "b2", "2", "b3", "3", "4", "#4/b5", "5", "b6", "6", "b7", "7"]
INTERVAL_NAME = ["unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th", "tritone",
                 "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th"]

# id, name, family, semitones, brightness (-1 dark .. 1 bright), tension (0 calm .. 1 tense), feeling, use
# Brightness and tension are design values for the visuals, not measurements: brightness follows the
# order of raised against lowered degrees (Persichetti's mode order, Lydian brightest, Locrian darkest),
# tension follows tritones, minor seconds and augmented steps in the set.
SCALES = [
    ("ionian", "Ionian (major)", "diatonic", [0, 2, 4, 5, 7, 9, 11], 0.6, 0.15, "bright, settled, happy, resolved", "pop, folk, classical"),
    ("dorian", "Dorian", "diatonic", [0, 2, 3, 5, 7, 9, 10], 0.1, 0.2, "minor but hopeful, cool, groovy", "funk, jazz, soul, Celtic"),
    ("phrygian", "Phrygian", "diatonic", [0, 1, 3, 5, 7, 8, 10], -0.6, 0.5, "dark, exotic, menacing (the b2)", "metal, flamenco, film"),
    ("lydian", "Lydian", "diatonic", [0, 2, 4, 6, 7, 9, 11], 0.9, 0.3, "floating, dreamy, wondrous (the #4)", "film scores, prog, ambient"),
    ("mixolydian", "Mixolydian", "diatonic", [0, 2, 4, 5, 7, 9, 10], 0.4, 0.2, "bright but bluesy, laid-back, rock", "rock, blues, Celtic"),
    ("aeolian", "Aeolian (natural minor)", "diatonic", [0, 2, 3, 5, 7, 8, 10], -0.3, 0.25, "sad, serious, reflective", "pop ballads, rock, EDM"),
    ("locrian", "Locrian", "diatonic", [0, 1, 3, 5, 6, 8, 10], -0.9, 0.8, "unstable, unresolved, eerie (the b5)", "metal, horror"),
    ("harmonic_minor", "Harmonic minor", "harmonic minor", [0, 2, 3, 5, 7, 8, 11], -0.4, 0.55, "dramatic, gothic, yearning (augmented 2nd)", "classical, metal, Middle Eastern pop"),
    ("locrian_n6", "Locrian natural 6", "harmonic minor", [0, 1, 3, 5, 6, 9, 10], -0.7, 0.75, "tense, dark, unsettled", "jazz, metal"),
    ("ionian_s5", "Ionian #5", "harmonic minor", [0, 2, 4, 5, 8, 9, 11], 0.5, 0.55, "bright yet strange, rising", "jazz, film"),
    ("dorian_s4", "Dorian #4 (Ukrainian Dorian)", "harmonic minor", [0, 2, 3, 6, 7, 9, 10], -0.1, 0.6, "folkloric, mysterious", "Eastern European folk, klezmer"),
    ("phrygian_dominant", "Phrygian dominant", "harmonic minor", [0, 1, 4, 5, 7, 8, 10], -0.2, 0.65, "exotic, fiery, Middle Eastern, Spanish", "flamenco, klezmer, metal"),
    ("lydian_s2", "Lydian #2", "harmonic minor", [0, 3, 4, 6, 7, 9, 11], 0.6, 0.6, "shimmering, alien", "jazz, film"),
    ("ultralocrian", "Ultralocrian (super Locrian bb7)", "harmonic minor", [0, 1, 3, 4, 6, 8, 9], -1.0, 0.9, "the darkest, collapsing", "jazz over diminished chords"),
    ("melodic_minor", "Melodic minor (jazz minor)", "melodic minor", [0, 2, 3, 5, 7, 9, 11], 0.0, 0.4, "bittersweet, sophisticated", "jazz, film"),
    ("dorian_b2", "Dorian b2 (Phrygian #6)", "melodic minor", [0, 1, 3, 5, 7, 9, 10], -0.4, 0.55, "dark and suspended", "jazz"),
    ("lydian_augmented", "Lydian augmented", "melodic minor", [0, 2, 4, 6, 8, 9, 11], 0.8, 0.6, "weightless, sci-fi, unresolved bright", "jazz, film"),
    ("lydian_dominant", "Lydian dominant (acoustic)", "melodic minor", [0, 2, 4, 6, 7, 9, 10], 0.7, 0.4, "playful, bright, cartoonish wonder", "jazz, video game music"),
    ("mixolydian_b6", "Mixolydian b6 (Aeolian dominant)", "melodic minor", [0, 2, 4, 5, 7, 8, 10], 0.0, 0.35, "melancholic but warm, cinematic", "film, prog"),
    ("locrian_s2", "Locrian #2 (half-diminished)", "melodic minor", [0, 2, 3, 5, 6, 8, 10], -0.7, 0.7, "dark, smoky, unresolved", "jazz"),
    ("altered", "Altered (super Locrian)", "melodic minor", [0, 1, 3, 4, 6, 8, 10], -0.8, 0.95, "maximum tension, about to resolve", "jazz dominants"),
    ("harmonic_major", "Harmonic major", "harmonic major", [0, 2, 4, 5, 7, 8, 11], 0.2, 0.5, "major with a dark twist (b6)", "classical, film"),
    ("major_pentatonic", "Major pentatonic", "pentatonic", [0, 2, 4, 7, 9], 0.6, 0.0, "open, sunny, innocent; no wrong notes", "pop, country, folk, East Asian"),
    ("suspended_pentatonic", "Suspended pentatonic (Egyptian)", "pentatonic", [0, 2, 5, 7, 10], 0.1, 0.05, "open, ancient, neither major nor minor", "folk, ambient"),
    ("blues_minor_pentatonic", "Blues minor pentatonic (Man Gong)", "pentatonic", [0, 3, 5, 8, 10], -0.5, 0.05, "dark, calm, meditative", "Chinese folk, ambient"),
    ("blues_major_pentatonic", "Blues major pentatonic (Ritusen, Yo)", "pentatonic", [0, 2, 5, 7, 9], 0.4, 0.0, "pastoral, light, folk", "Japanese folk, Celtic"),
    ("minor_pentatonic", "Minor pentatonic", "pentatonic", [0, 3, 5, 7, 10], -0.3, 0.05, "bluesy, earthy, strong", "blues, rock, hip-hop, R&B"),
    ("hirajoshi", "Hirajoshi", "pentatonic", [0, 2, 3, 7, 8], -0.5, 0.35, "Japanese, melancholic, still", "Japanese koto, film"),
    ("in_sen", "In (Miyako-bushi)", "pentatonic", [0, 1, 5, 7, 8], -0.7, 0.45, "sombre, austere, Japanese", "Japanese traditional"),
    ("iwato", "Iwato", "pentatonic", [0, 1, 5, 6, 10], -0.9, 0.7, "dark, ritual, uneasy", "Japanese traditional, metal"),
    ("blues", "Blues (minor hexatonic)", "blues", [0, 3, 5, 6, 7, 10], -0.3, 0.35, "gritty, soulful, crying (the blue note b5)", "blues, rock, jazz"),
    ("major_blues", "Major blues", "blues", [0, 2, 3, 4, 7, 9], 0.4, 0.25, "cheerful with a sly bend", "country, blues, jazz"),
    ("whole_tone", "Whole tone", "symmetric", [0, 2, 4, 6, 8, 10], 0.5, 0.55, "dreamlike, blurry, floating, no home", "impressionism, dream sequences"),
    ("augmented", "Augmented (hexatonic)", "symmetric", [0, 3, 4, 7, 8, 11], 0.3, 0.6, "mirrored, magical, uncanny", "jazz, film"),
    ("diminished_hw", "Diminished half-whole", "symmetric", [0, 1, 3, 4, 6, 7, 9, 10], -0.4, 0.85, "suspense, chase, danger", "jazz, thriller, film"),
    ("diminished_wh", "Diminished whole-half", "symmetric", [0, 2, 3, 5, 6, 8, 9, 11], -0.5, 0.85, "spiralling, ominous", "jazz, horror"),
    ("chromatic", "Chromatic", "symmetric", list(range(12)), 0.0, 1.0, "no key: noise, atonal, chaos", "atonal, sound design"),
    ("bebop_dominant", "Bebop dominant", "bebop", [0, 2, 4, 5, 7, 9, 10, 11], 0.4, 0.3, "swinging, busy, jazzy", "bebop jazz"),
    ("bebop_major", "Bebop major", "bebop", [0, 2, 4, 5, 7, 8, 9, 11], 0.5, 0.3, "elegant, swinging", "bebop jazz"),
    ("double_harmonic", "Double harmonic major (Byzantine)", "exotic", [0, 1, 4, 5, 7, 8, 11], -0.1, 0.75, "exotic, desert, intense", "Arabic, Indian, surf rock"),
    ("hungarian_minor", "Hungarian minor", "exotic", [0, 2, 3, 6, 7, 8, 11], -0.5, 0.75, "gypsy, passionate, dramatic", "Romani, Eastern European, metal"),
    ("neapolitan_minor", "Neapolitan minor", "exotic", [0, 1, 3, 5, 7, 8, 11], -0.7, 0.7, "dark, operatic, tragic", "classical, metal"),
    ("neapolitan_major", "Neapolitan major", "exotic", [0, 1, 3, 5, 7, 9, 11], 0.0, 0.65, "strange, noble", "classical"),
    ("enigmatic", "Enigmatic", "exotic", [0, 1, 4, 6, 8, 10, 11], 0.4, 0.85, "riddle-like, unstable", "Verdi, experimental"),
    ("persian", "Persian", "exotic", [0, 1, 4, 5, 6, 8, 11], -0.6, 0.85, "mystic, heavy, tense", "Persian-flavoured rock, film"),
    ("prometheus", "Prometheus", "exotic", [0, 2, 4, 6, 9, 10], 0.6, 0.5, "mystical, luminous, unresolved", "Scriabin, film"),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "notes-hz.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["midi", "name_sharp", "name_flat", "octave", "hz", "pitch_class", "period_ms"])
        for midi in range(0, 136):
            hz = 440.0 * 2 ** ((midi - 69) / 12)
            if hz < 20.0 or hz > 20000.0:
                continue
            pc = midi % 12
            octave = midi // 12 - 1
            w.writerow([midi, f"{SHARP[pc]}{octave}", f"{FLAT[pc]}{octave}", octave, f"{hz:.4f}", pc, f"{1000.0 / hz:.4f}"])
    with open(os.path.join(OUT, "scales.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["id", "name", "family", "notes", "semitones", "steps", "degrees", "intervals",
                    "brightness", "tension", "feeling", "used_in"])
        for sid, name, family, st, bright, tension, feeling, use in SCALES:
            steps = [(st[(i + 1) % len(st)] - st[i]) % 12 for i in range(len(st))]
            w.writerow([sid, name, family, len(st), " ".join(map(str, st)), " ".join(map(str, steps)),
                        " ".join(INTERVAL[s] for s in st), "; ".join(INTERVAL_NAME[s] for s in st),
                        bright, tension, feeling, use])
    with open(os.path.join(OUT, "scales-all-roots.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["id", "root", "notes_sharp", "notes_flat"])
        for sid, *_rest in SCALES:
            st = _rest[2]
            for root in range(12):
                w.writerow([sid, SHARP[root], " ".join(SHARP[(root + s) % 12] for s in st),
                            " ".join(FLAT[(root + s) % 12] for s in st)])
    inc = os.path.join(os.path.dirname(__file__), "..", "core", "sound", "ScaleTable.inc")
    with open(inc, "w") as f:
        f.write("// Generated by tools/musical_intelligence.py from its SCALES table. Do not edit: edit the script.\n")
        f.write("// {id, pitch-class mask (bit 0 = the root), brightness -1..1, tension 0..1}, in scales.csv order.\n")
        for sid, _name, _family, st, bright, tension, *_ in SCALES:
            mask = sum(1 << s for s in st)
            f.write(f'{{"{sid}", 0x{mask:03x}, {bright:.2f}f, {tension:.2f}f}},\n')


if __name__ == "__main__":
    main()
