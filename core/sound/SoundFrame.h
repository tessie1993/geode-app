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
 *  - Triggers (kick … reattack): > 0 on the one frame they fire (the value is the strength, 0..1), else 0. */

#define GEODE_SOUND_EQ_BANDS 16
#define GEODE_SOUND_SPECTRUM_POINTS 64
#define GEODE_SOUND_WAVEFORM_POINTS 128
#define GEODE_SOUND_SCALARS 74
#define GEODE_SOUND_FRAME_FLOATS \
    (GEODE_SOUND_SCALARS + GEODE_SOUND_EQ_BANDS + GEODE_SOUND_SPECTRUM_POINTS + GEODE_SOUND_WAVEFORM_POINTS)

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
     * gain and offset (typical sits at 0.5). level is the whole mix over its own slow peak. */
    float low;
    float middle;
    float high;
    float level;
    /* Slow trends, 0..1, that ignore single hits: each band's activity over ~3 s (Synesthesia's presence),
     * and intensity, which creeps toward 1 the longer the song stays louder than its own average. */
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
    /* TouchDesigner Band EQ centres (25 Hz … 22 kHz), each relative. */
    float eq[GEODE_SOUND_EQ_BANDS];
    /* Log-spaced spectrum 20 Hz–20 kHz, tilted like the bands, over a slow peak: 0..1 for textures. */
    float spectrum[GEODE_SOUND_SPECTRUM_POINTS];
    /* The newest ~13 ms of the mono mix, aligned to the previous frame so a steady tone stands still. */
    float waveform[GEODE_SOUND_WAVEFORM_POINTS];
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
} GeodeSoundTuning;

#define GEODE_SOUND_TUNING_FLOATS 29
