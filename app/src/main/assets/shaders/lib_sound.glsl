// The sound engine's frame (core/sound/SoundFrame.h), raw, for any style that includes this.
//
// uSound holds every scalar of GeodeSoundFrame in declaration order, four to a vec4; the SND_* names
// below are generated from the header, and SoundFrameLayoutTest / ShaderSoundLibTest fail if they drift.
// What each kind of value means (relative ~1.0, shaped 0..1, clocks, one-frame triggers) is documented
// on the struct. Nothing here is driven, clamped or slewed: shape it in the style.
//
// uSoundTex (R32F, 128 x 3): row 0 the 64-point log spectrum 0..1, row 1 the stabilised waveform -1..1,
// row 2 the 16 EQ bands (relative). sndSpectrum / sndWave / sndEq read them with x in 0..1.

precision highp sampler2D;

uniform vec4 uSound[19];
uniform sampler2D uSoundTex;

#define SND_BASS               uSound[0].x
#define SND_MID                uSound[0].y
#define SND_TREBLE             uSound[0].z
#define SND_VOL                uSound[0].w
#define SND_BASS_ATT           uSound[1].x
#define SND_MID_ATT            uSound[1].y
#define SND_TREBLE_ATT         uSound[1].z
#define SND_VOL_ATT            uSound[1].w
#define SND_SUB                uSound[2].x
#define SND_SUB_ATT            uSound[2].y
#define SND_MID_HIGH           uSound[2].z
#define SND_MID_HIGH_ATT       uSound[2].w
#define SND_LOW                uSound[3].x
#define SND_MIDDLE             uSound[3].y
#define SND_HIGH               uSound[3].z
#define SND_LEVEL              uSound[3].w
#define SND_BASS_PRESENCE      uSound[4].x
#define SND_MID_PRESENCE       uSound[4].y
#define SND_TREBLE_PRESENCE    uSound[4].z
#define SND_INTENSITY          uSound[4].w
#define SND_BASS_TIME          uSound[5].x
#define SND_MID_TIME           uSound[5].y
#define SND_TREBLE_TIME        uSound[5].z
#define SND_CURVED_TIME        uSound[5].w
#define SND_HIT_TIME           uSound[6].x
#define SND_NOTE_TIME          uSound[6].y
#define SND_PRESENCE           uSound[6].z
#define SND_RMS                uSound[6].w
#define SND_LOUDNESS           uSound[7].x
#define SND_CENTROID           uSound[7].y
#define SND_ROLLOFF            uSound[7].z
#define SND_FLATNESS           uSound[7].w
#define SND_DENSITY_SLOW       uSound[8].x
#define SND_DENSITY_FAST       uSound[8].y
#define SND_FLUX               uSound[8].z
#define SND_ENERGY_TREND       uSound[8].w
#define SND_PEAK_FREQ          uSound[9].x
#define SND_PEAK_MAG           uSound[9].y
#define SND_STEREO_WIDTH       uSound[9].z
#define SND_STEREO_PAN         uSound[9].w
#define SND_KICK_ENV           uSound[10].x
#define SND_SNARE_ENV          uSound[10].y
#define SND_HAT_ENV            uSound[10].z
#define SND_ONSET_ENV          uSound[10].w
#define SND_KICK_TOGGLE        uSound[11].x
#define SND_SNARE_TOGGLE       uSound[11].y
#define SND_KICK_RANDOM        uSound[11].z
#define SND_NOTE_RANDOM        uSound[11].w
#define SND_KICK               uSound[12].x
#define SND_SNARE              uSound[12].y
#define SND_HAT                uSound[12].z
#define SND_KICK2              uSound[12].w
#define SND_KICK4              uSound[13].x
#define SND_KICK8              uSound[13].y
#define SND_KICK16             uSound[13].z
#define SND_SNARE2             uSound[13].w
#define SND_SNARE4             uSound[14].x
#define SND_HARD_CUT           uSound[14].y
#define SND_ONSET              uSound[14].z
#define SND_SILENCE            uSound[14].w
#define SND_RETURNED           uSound[15].x
#define SND_SURGE              uSound[15].y
#define SND_NOTE_CHANGE        uSound[15].z
#define SND_LEGATO             uSound[15].w
#define SND_REATTACK           uSound[16].x
#define SND_PITCH              uSound[16].y
#define SND_PITCH_CLASS        uSound[16].z
#define SND_PITCH_HEIGHT       uSound[16].w
#define SND_PITCH_CONFIDENCE   uSound[17].x
#define SND_INTERVAL           uSound[17].y
#define SND_GLIDE              uSound[17].z
#define SND_VIBRATO            uSound[17].w
#define SND_EVENNESS           uSound[18].x
#define SND_RUN_DENSITY        uSound[18].y

float sndSpectrum(float x) { return texture(uSoundTex, vec2(clamp(x, 0.0, 1.0), 0.5 / 3.0)).r; }
float sndWave(float x) { return texture(uSoundTex, vec2(clamp(x, 0.0, 1.0), 1.5 / 3.0)).r; }
float sndEq(float x) { return texture(uSoundTex, vec2(clamp(x, 0.0, 1.0), 2.5 / 3.0)).r; }
