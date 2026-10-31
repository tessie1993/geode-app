#pragma once

/* What the sound engine hears in one rendered frame. Plain C and all floats, so the whole frame crosses JNI
 * as one float array in declaration order and the C++ renderer, the shaders and Kotlin read the same thing.
 *
 * Three families of value, after MilkDrop 3 and TouchDesigner:
 *  - Relative levels (bass … volAtt, eq, flux): a band's energy over its own ~4 s running mean, so 1.0 is
 *    "typical for this song right now", 0.7 reads quiet and 1.3 loud. Unbounded above; 2–3 on a hit.
 *  - Shaped values (low, middle, high, descriptors, envelopes): 0..1, pan -1..1.
 *  - Triggers (kick … legato): > 0 on the one frame they fire (the value is the strength, 0..1), else 0. */

#define GEODE_SOUND_EQ_BANDS 16
#define GEODE_SOUND_SPECTRUM_POINTS 64
#define GEODE_SOUND_WAVEFORM_POINTS 128
#define GEODE_SOUND_SCALARS 49
#define GEODE_SOUND_FRAME_FLOATS \
    (GEODE_SOUND_SCALARS + GEODE_SOUND_EQ_BANDS + GEODE_SOUND_SPECTRUM_POINTS + GEODE_SOUND_WAVEFORM_POINTS)

typedef struct GeodeSoundFrame {
    /* Relative levels, the MilkDrop core. Bands: bass 20–250 Hz, mid 250 Hz–4 kHz, treble 4–16 kHz.
     * *Att is the same band through a fast envelope (~20 ms up, ~50 ms down). vol is the mean of the three. */
    float bass;
    float mid;
    float treble;
    float vol;
    float bassAtt;
    float midAtt;
    float trebleAtt;
    float volAtt;
    /* The TouchDesigner layer: bass/mid/treble att through the user's smooth, threshold and gain, 0..1. */
    float low;
    float middle;
    float high;
    /* Seconds that run faster while the band is loud (dt × att each frame), for motion that surges with
     * the music but never jumps back. Wrap at an hour. */
    float bassTime;
    float midTime;
    float trebleTime;
    /* Event clocks: +1 per drum hit (kick or snare) / per note change, eased over ~150 ms, so motion steps
     * with the music without a tempo. Wrap at 4096. */
    float hitTime;
    float noteTime;
    /* 0..1 fade that rises over ~2 s while music plays and falls over ~2 s of silence. */
    float presence;
    /* Descriptors. */
    float rms;          /* window RMS of the mono mix, linear full scale */
    float centroid;     /* spectral centre on a log scale, 0 = 20 Hz, 1 = 20 kHz */
    float flatness;     /* 0 tonal … 1 noise, 100 Hz–10 kHz */
    float densitySlow;  /* how many partials sound at once, 0..1, ~1 s smoothing */
    float densityFast;  /* the same, ~0.1 s */
    float flux;         /* positive spectral change, relative level */
    float energyTrend;  /* short (0.4 s) over long (8 s) loudness, halved: 0.5 steady, 1 = twice as loud */
    float stereoWidth;  /* 0 mono, 0.5 unrelated channels, 1 out of phase */
    float stereoPan;    /* -1 left … 1 right */
    /* Drum envelopes: jump on the drum's trigger, then decay. 0..1. */
    float kickEnv;
    float snareEnv;
    float hatEnv;
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
    float silence;      /* the music just stopped (0.5 s under -60 dBFS) */
    float returned;     /* sound came back after at least a second of silence */
    float surge;        /* loudness jumped after a quieter passage */
    float noteChange;   /* the lead pitch moved to a new note */
    float legato;       /* … and did so without an attack: a slide, bend or tied note */
    /* Melody, from the strongest harmonic series between 80 Hz and 1.5 kHz. */
    float pitch;        /* MIDI note number with cents as the fraction; 0 when no clear pitch */
    float pitchHeight;  /* 0 at C2 … 1 at C6, held while no pitch */
    float pitchConfidence;
    float runDensity;   /* note changes per second over 12, 0..1: busy runs vs held notes */
    /* TouchDesigner Band EQ centres (25 Hz … 22 kHz), each a relative level. */
    float eq[GEODE_SOUND_EQ_BANDS];
    /* Log-spaced spectrum 20 Hz–20 kHz, tilted like the bands, over a slow peak: 0..1 for textures. */
    float spectrum[GEODE_SOUND_SPECTRUM_POINTS];
    /* The newest ~13 ms of the mono mix, aligned to the previous frame so a steady tone stands still. */
    float waveform[GEODE_SOUND_WAVEFORM_POINTS];
} GeodeSoundFrame;

/* User tuning, global (Settings › Audio). Index 0..2 = low, middle, high; drums = kick, snare, hat. */
typedef struct GeodeSoundTuning {
    float gain[3];       /* 0..4, default 1 */
    float threshold[3];  /* 0..1, default 0: subtracted after halving, so 1.0 relative reads 0.5 */
    float smooth[3];     /* seconds, 0..1, default 0.08 */
    float drum[3];       /* punch a hit must reach, 0.1..3; defaults 0.6, 0.5, 0.5 */
    float reactivity;    /* one multiplier on the low/middle/high gains, 0..3, default 1 */
    float speed;         /* one multiplier on every clock (band and event times), 0..4, default 1 */
} GeodeSoundTuning;

#define GEODE_SOUND_TUNING_FLOATS 14
