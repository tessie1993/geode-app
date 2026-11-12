#pragma once

/* What the sound engine hears in one rendered frame. Plain C and all floats, so the whole frame crosses JNI
 * as one float array in declaration order, and the C++ renderer, the shaders and Kotlin read the same thing.
 *
 * Where the parameters come from: MilkDrop 3 (the relative levels and their att envelopes, the hard cut,
 * the stabilised waveform), TouchDesigner (the shaped low/middle/high with gain, threshold, offset, attack
 * and release; kick/snare/hat triggers with hysteresis and envelopes; counted hits; centroid and partial
 * density; the Band EQ), and other VJ tools (Synesthesia's band clocks, presence, intensity, toggle- and
 * random-on-hit; VDMX's peak frequency; sigmund~/fiddle~'s note, re-attack and evenness; projectM's
 * frame-rate-independent rates).
 *
 * Kinds of value:
 *  - Relative (bass … midHighAtt, flux, eq): energy over its own ~4 s running mean. 1.0 is typical for
 *    this song right now, 0.7 reads quiet, 1.3 loud, 2 a strong hit; bent smoothly so nothing passes 4.
 *  - Shaped (everything marked 0..1): ready to drive a parameter as it is.
 *  - Clocks (…Time): seconds or counts that only move forward, at a speed the music sets. Wrap at an hour
 *    (band clocks) or at 4096 (event clocks).
 *  - Triggers (kick … reattack, transient): > 0 on the one frame they fire (the value is the strength, 0..1), else 0.
 *  - Told (fadeGain): what the player says, not what the engine hears. */

#define GEODE_SOUND_EQ_BANDS 16
#define GEODE_SOUND_SPECTRUM_POINTS 64
#define GEODE_SOUND_WAVEFORM_POINTS 128
#define GEODE_SOUND_CHROMA 12
#define GEODE_SOUND_SCALARS 116
#define GEODE_SOUND_FRAME_FLOATS                                                                       \
    (GEODE_SOUND_SCALARS + GEODE_SOUND_EQ_BANDS + GEODE_SOUND_SPECTRUM_POINTS + GEODE_SOUND_WAVEFORM_POINTS + \
     3 * GEODE_SOUND_CHROMA)

/* sectionState: what part of the song this is, inferred live (core/sound/Form). */
#define GEODE_SECTION_INTRO 0
#define GEODE_SECTION_VERSE 1
#define GEODE_SECTION_BUILD 2
#define GEODE_SECTION_DROP 3
#define GEODE_SECTION_BREAKDOWN 4
#define GEODE_SECTION_OUTRO 5

/* The slowest and fastest beat the tempo detector can measure (three periods in its 6 s window, a
 * 0.2 s period). Limits of the measurement, flat in between: no tempo inside them is weighted. */
#define GEODE_SOUND_BPM_SLOWEST 30.0f
#define GEODE_SOUND_BPM_FASTEST 300.0f

typedef struct GeodeSoundFrame {
    /* Relative levels, the MilkDrop core, over a +6 dB/octave tilted spectrum. bass 20–250 Hz, mid 250 Hz–
     * 4 kHz, treble 4–16 kHz; sub 20–60 Hz and midHigh 2–6 kHz split out the floor and the presence region.
     * *Att is the same band through a fast envelope (~20 ms up, ~50 ms down). vol is the mean of the three. */
    float bass;
    float mid;
    float treble;
    float vol;
    float bassAtt;
    float midAtt;
    float trebleAtt;
    float volAtt;
    float sub;
    float subAtt;
    float midHigh;
    float midHighAtt;
    /* The TouchDesigner layer, 0..1: bass/mid/treble att through the user's attack, release, threshold,
     * gain and offset (typical sits at 0.5). level is the whole mix's absolute loudness. */
    float low;
    float middle;
    float high;
    float level;        /* absolute: the mix's RMS in dBFS, -60 dB = 0 … 0 dB = 1 (no peak it is measured against) */
    /* Slow trends, 0..1, that ignore single hits: each band's activity over ~3 s (Synesthesia's presence),
     * and intensity: drive (below) plus how far the momentary loudness has risen over the short-term one. */
    float bassPresence;
    float midPresence;
    float treblePresence;
    float intensity;
    /* Clocks. bassTime… advance dt × the band's att each frame; curvedTime advances with vol_att^2.5, so
     * quiet barely moves it and loud races. hitTime / noteTime count drum hits / note changes, eased over
     * ~150 ms. presence is a 0..1 fade: up over ~2 s while music plays, down over ~2 s of silence. */
    float bassTime;
    float midTime;
    float trebleTime;
    float curvedTime;
    float hitTime;
    float noteTime;
    float presence;
    /* Descriptors. */
    float rms;          /* window RMS of the mono mix, linear full scale */
    float loudness;     /* rms in dBFS, -60 dB = 0 … 0 dB = 1 */
    float centroid;     /* spectral centre of mass on a log axis, 0 = 20 Hz … 1 = 20 kHz */
    float rolloff;      /* frequency under which 85% of the spectrum lies, same axis */
    float flatness;     /* tonal 0 … noise 1, in dB: -30 dB = 0 */
    float densitySlow;  /* how many partials sound at once, 0..1, ~1 s */
    float densityFast;  /* the same, ~0.1 s */
    float flux;         /* positive spectral change, relative */
    float energyTrend;  /* short (0.4 s) over long (8 s) loudness, halved: 0.5 steady, 1 = twice as loud */
    float peakFreq;     /* the strongest bin, same log axis as centroid */
    float peakMag;      /* its level over a slow peak, 0..1 */
    float stereoWidth;  /* 0 mono, 0.5 unrelated channels, 1 out of phase */
    float stereoPan;    /* -1 left … 1 right */
    /* Envelopes and eased per-hit values, 0..1. The drum and onset envelopes jump on their trigger and
     * decay. Toggles flip between 0 and 1 on each hit, randoms take a new value on each hit (kick) or note
     * change; both ease over ~60 ms. The random sequence is fixed, so live and export draw the same one. */
    float kickEnv;
    float snareEnv;
    float hatEnv;
    float onsetEnv;
    float kickToggle;
    float snareToggle;
    float kickRandom;
    float noteRandom;
    /* Triggers. kickN / snareN fire on every Nth hit of that drum. */
    float kick;
    float snare;
    float hat;
    float kick2;
    float kick4;
    float kick8;
    float kick16;
    float snare2;
    float snare4;
    float hardCut;      /* everything jumped past an adaptive threshold that then backs off (a "big moment") */
    float onset;        /* any instrument starting a note: a flux peak */
    float silence;      /* the music just stopped */
    float returned;     /* sound came back after at least a second of silence */
    float surge;        /* loudness jumped after a quieter passage */
    float noteChange;   /* the lead pitch moved to a new note */
    float legato;       /* … and did so without an attack: a slide, bend or tied note */
    float reattack;     /* the same note struck again */
    /* Melody, from the strongest harmonic series between 80 Hz and 1.5 kHz. Held while there is no pitch. */
    float pitch;        /* MIDI note number, cents as the fraction; 0 while unvoiced */
    float pitchClass;   /* C = 0 … B = 11/12, for hue */
    float pitchHeight;  /* 0 at C2 … 1 at C6 */
    float pitchConfidence;
    float interval;     /* the last note change in octaves, -1 … 1 (a fifth up = 7/12) */
    float glide;        /* how fast the pitch slides, 0 … 1 at 24 semitones a second */
    float vibrato;      /* wobble around the held note, 0 … 1 at half a semitone */
    float evenness;     /* share of even harmonics: hollow, square-ish tones read low */
    float runDensity;   /* note changes per second over 12: busy runs vs held notes */
    /* The musical layer. Rhythm, from core/sound/Rhythm: an onset envelope
     * at a fixed 100 Hz hop, its autocorrelation over 6 s for the tempo, and a phase-locked beat oscillator.
     * While beatConf is low the phases fall back to the hit clock (beat = one hit, bar = four) and the
     * beat, downbeat and phrase triggers stay silent. The tempo is the music's own: no tempo is preferred
     * or assumed, and before one is heard bpm is 0 and beatCount, barCount and the triggers stay still. */
    float bpm;          /* tempo, beats per minute, 0 = none heard yet (never a stand-in number); the last heard
                           while unlocked. Within GEODE_SOUND_BPM_SLOWEST..FASTEST, the detector's search limits */
    float beatPhase;    /* 0 on the beat … 1 just before the next: 1 - beatPhase is how soon it lands */
    float beatConf;     /* how sure the tracker is of the beat, 0..1 (locked above ~0.35) */
    float beat;         /* trigger on each beat the oscillator predicts */
    float beatCount;    /* beats so far, a count wrapping at 4096 */
    float downbeat;     /* trigger on the first beat of each bar (the beat slot the kicks favour, 4/4) */
    float barPhase;     /* 0 on the downbeat … 1 at the end of the bar */
    float barCount;     /* bars so far, wrapping at 4096 */
    float phrase;       /* trigger on the first downbeat of each 8-bar phrase */
    float phraseIndex;  /* phrases so far, wrapping at 4096; a section change starts a new one */
    float phrasePhase;  /* 0 at the phrase's first downbeat … 1 at the end of its eighth bar */
    /* Onset kinds: positive change in the low (< 250 Hz), mid and high filter-bank bands, relative like
     * flux; hfc is high-frequency content (Masri), relative: percussive onsets spike it, tonal ones barely. */
    float fluxLow;
    float fluxMid;
    float fluxHigh;
    float hfc;
    /* Harmony, from core/sound/Harmony: the chroma (below) against Noland's key profiles. */
    float keyHue;       /* the key on the circle of fifths, 0..1 (C = 0, G = 1/12 …; a minor key sits on its relative major) */
    float keyRoot;      /* the key's tonic pitch class, C = 0 … B = 11/12 */
    float keyMajor;     /* 1 major … 0 minor, 0.5 undecided */
    float keyClarity;   /* how well the best key fits, 0..1 */
    float chordHue;     /* the medium chroma's centre on the circle of fifths, 0..1: moves with each chord */
    float tension;      /* dissonance of the medium chroma (seconds, tritones, sevenths sounding together), 0..1 */
    float harmonicChange; /* trigger: the last half second's chroma moved away from the two seconds before */
    /* Scale and mode, from the musical intelligence database (docs/musical-intelligence, core/sound/ScaleTable.inc):
     * the catalogue entry (46 scales and modes: the church modes, harmonic and melodic minor and their
     * modes, pentatonics, blues, symmetric and exotic scales) that best fits the slow chroma on its root. */
    float scale;        /* the catalogue index, in scales.csv order; -1 before any music */
    float scaleRoot;    /* the scale's root pitch class, C = 0 … B = 11/12 */
    float scaleBrightness; /* the scale's mood: 0 darkest (Locrian, super Locrian) … 1 brightest (Lydian), 0.5 neutral */
    float scaleTension; /* how unstable the scale sounds (tritones, minor seconds, augmented steps), 0..1 */
    float scaleFit;     /* how clearly the music sits in that scale, 0 (atonal, noise) … 1 */
    /* Form, from core/sound/Form: per-beat features in a 32-beat ring, Foote's checkerboard novelty, and
     * the energy arc. */
    float novelty;      /* how unlike the bar just played is to the 16 beats before it, 0..1 */
    float sectionChange; /* trigger on a downbeat, at least 8 bars after the last: a new section began a bar ago.
                          * 0, or its strength at 0.4..1 (Form::kEventFloor): act on > 0 */
    float sectionId;    /* which section this is, 0..7; a returning chorus gets its old number back */
    float sectionState; /* GEODE_SECTION_*: intro, verse, build, drop, breakdown, outro */
    float sectionBars;  /* bars since the section (state) began, wrapping at 4096 */
    float buildUp;      /* 0..1: a build is under way (louder, brighter, busier, bass pulled out) */
    float drop;         /* trigger: the bass came back after a build. 0, or the build's peak at 0.4..1
                         * (Form::kEventFloor): act on > 0 */
    /* The player (core/sound/PlayerSignals), told rather than heard. A track boundary the player names (a
     * gapless or crossfaded next track, which the tap cannot hear) also fires sectionChange at full strength
     * (1) on the frame it lands, at once rather than on a downbeat: the next song is the biggest section. */
    float fadeGain;     /* the player's fades, 0..1: pause and skip fades times the crossfade; 1 while none runs,
                         * under the mic or another app's audio, and always in an export. The tap is before the volume, so nothing else hears it */
    /* Absolute loudness and drive (core/sound/LufsMeter, fosfora's LoudnessMeter): none of these is relative to
     * the song. loudnessM / loudnessS are BS.1770 K-weighted momentary (400 ms) and short-term (3 s) loudness,
     * -60 LUFS = 0 … 0 LUFS = 1. busy is the texture, 0..1 on fixed scales: onsets per second (8 a second = 1),
     * the drums' share and the tracker's beat confidence. drive is THE magnitude of the music, 0..1: short-term
     * loudness × (0.35 + 0.65 · busy), eased (0.5 s up, 1.5 s down). An ambient pad at -25 LUFS reads ~0.2,
     * a loud dense kick and bass at -9 LUFS ~0.75; at equal loudness a sparse passage reads under a dense one.
     * Everything that scales how MUCH the picture moves reads drive; the relative fields above only shape
     * the pulse inside it. */
    float loudnessM;
    float loudnessS;
    float busy;
    float drive;
    /* Transients (core/sound/Transient): the sharp start of any sound, not only a drum's, seen per sample in each
     * filter-bank band and the whole mix within a few milliseconds (onset above is a flux peak, ~70 ms late).
     * Only above the silence gate and the hit floor, and worth more with the drive, as every hit is. */
    float transient;          /* trigger: an attack, its strength 0..1 */
    float transientEnv;       /* its envelope, onsetEnv's rise and fall (5 ms up, 0.2 s down) */
    float transientSharpness; /* how steep the last attack was, 0 a bare pass … 1 a step: held until the next */
    /* TouchDesigner Band EQ centres (25 Hz … 22 kHz), each relative. */
    float eq[GEODE_SOUND_EQ_BANDS];
    /* Log-spaced spectrum 20 Hz–20 kHz, tilted like the bands, over a slow peak: 0..1 for textures. */
    float spectrum[GEODE_SOUND_SPECTRUM_POINTS];
    /* The newest ~13 ms of the mono mix, aligned to the previous frame so a steady tone stands still. */
    float waveform[GEODE_SOUND_WAVEFORM_POINTS];
    /* Chroma: energy per pitch class (C = 0 … B = 11), 250 Hz–2 kHz, over its own maximum (the loudest
     * class reads 1). Medium (~0.5 s), fast (~0.1 s) and slow (~4 s, what the key reads) smoothing. */
    float chroma[GEODE_SOUND_CHROMA];
    float chromaFast[GEODE_SOUND_CHROMA];
    float chromaSlow[GEODE_SOUND_CHROMA];
} GeodeSoundFrame;

/* User tuning, global (Settings › Audio). [3] arrays are low, middle, high, or kick, snare, hat. */
typedef struct GeodeSoundTuning {
    float gain[3];          /* 0..4, default 1 */
    float threshold[3];     /* 0..1, default 0: subtracted after halving, so typical reads 0.5 */
    float offset[3];        /* -1..1, default 0: added last (TouchDesigner's "add") */
    float attack[3];        /* seconds to rise, 0..1, default 0.03 */
    float release[3];       /* seconds to fall, 0..2, default 0.15 */
    float drum[3];          /* punch a hit must reach, 0.1..3; defaults 0.6, 0.5, 0.5 */
    float drumDecay[3];     /* envelope decay in seconds; defaults 0.18, 0.15, 0.08 */
    float reactivity;       /* one multiplier on every shaped band, 0..3, default 1 */
    float speed;            /* one multiplier on every clock, 0..4, default 1 */
    float onsetThreshold;   /* flux a note start must reach, relative, default 2 */
    float hardCutLevel;     /* the hard cut's resting threshold (mean relative level), default 2.5 */
    float hardCutHalfLife;  /* seconds for the raised threshold to settle back, default 60 */
    float pitchThreshold;   /* confidence that counts as a pitch, 0..1, default 0.35 */
    float noteHold;         /* seconds a new note must hold before it counts, default 0.08 */
    float silenceDb;        /* below this (dBFS) for half a second is silence, default -60 */
    float inputTrimDb;      /* analysis-only input gain, -24..24 dB, default 0: for a mic or another app's audio of unknown gain */
} GeodeSoundTuning;

#define GEODE_SOUND_TUNING_FLOATS 30
/* Each field's first slot in the wire form; a [3] field takes three in a row. */
#define GEODE_SOUND_TUNING_GAIN 0
#define GEODE_SOUND_TUNING_THRESHOLD 3
#define GEODE_SOUND_TUNING_OFFSET 6
#define GEODE_SOUND_TUNING_ATTACK 9
#define GEODE_SOUND_TUNING_RELEASE 12
#define GEODE_SOUND_TUNING_DRUM 15
#define GEODE_SOUND_TUNING_DRUM_DECAY 18
#define GEODE_SOUND_TUNING_REACTIVITY 21
#define GEODE_SOUND_TUNING_SPEED 22
#define GEODE_SOUND_TUNING_ONSET_THRESHOLD 23
#define GEODE_SOUND_TUNING_HARD_CUT_LEVEL 24
#define GEODE_SOUND_TUNING_HARD_CUT_HALF_LIFE 25
#define GEODE_SOUND_TUNING_PITCH_THRESHOLD 26
#define GEODE_SOUND_TUNING_NOTE_HOLD 27
#define GEODE_SOUND_TUNING_SILENCE_DB 28
#define GEODE_SOUND_TUNING_INPUT_TRIM_DB 29
