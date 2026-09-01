// The uniform block, the two audio taps and view() that EVERY fragment style
// shares.
//
// This is a verbatim lift out of kaleido_frag.glsl, which was the canonical
// copy: the same block, comments included, was pasted into all 22 fragment
// styles. Duplication that size is not a style problem, it is a correctness
// one - "Drift ping-pongs rather than running away" was fixed in view() once
// and had to land in 22 files, and any file that missed a fix quietly became a
// second behaviour under the same slider. New styles include this instead, so
// there is one view() to reason about and one place a fix has to land.
//
// The 22 existing styles are deliberately NOT converted. Rewriting a shader
// that is on screen today to prove a refactor is how a working style acquires
// a diff nobody can review; they carry their own copy until something else
// gives a reason to touch them.
//
// A style includes this, then lib_palette, then lib_scene_grade - grade()
// calls pal(), so the palette has to be declared between them.

// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized. Declared here as well as at the
// top of the style file so an including shader that forgot its own precision
// statement still reads full-precision texels.
precision highp sampler2D;

uniform float uTime;
uniform vec2 uResolution;
uniform float uBass;
uniform float uMid;
uniform float uTreble;
uniform float uEnergy;
uniform float uBeat;
uniform sampler2D uAudioTex;
uniform float uSpeed;
uniform float uZoom;
uniform float uRotation;
uniform float uZoomPhase;
uniform float uColorShift;
uniform float uHueRange;
uniform float uSat;
uniform float uBright;
uniform float uInvert;
uniform float uIntensity;
uniform float uMirrorX;
uniform float uBeatResponse;
uniform float uTurbulence;
uniform float uPalBase;
uniform float uPalRange;
uniform float uContrast;
uniform float uGamma;
uniform float uPal2Base;
uniform float uPal2Range;
uniform float uPaletteMix;
uniform float uDuotone;
uniform float uBloom;
uniform float uWarp;
uniform float uRipple;
uniform float uSymmetry;
uniform float uKaleido;
uniform float uMorph;
uniform float uPixelate;
uniform float uPosterize;
uniform float uSway;
uniform float uPulse;
uniform float uBeatPhase;
uniform float uDriftX;
uniform float uDriftY;
uniform float uShake;
uniform float uTile;
uniform float uTwist;
uniform float uTemperature;
uniform float uSolarize;
uniform float uFlash;

// ---- where the fingers are ------------------------------------------------
//
// dev.geode.render.TouchField is the model; ShaderScene.uploadTouch() is the
// upload. All of it is ZERO when nothing is being touched, so a style that
// ignores these draws exactly what it drew before they existed.
//
// COORDINATES: y-up NDC. x and y in -1..1, origin at the centre of the
// surface, +y toward the top. Aspect is NOT pre-applied - multiply x by
// uResolution.x/uResolution.y yourself, exactly as view() does on its first
// two lines, or a finger on a landscape phone lands somewhere the user did
// not put it. lib_touch.glsl does that correction for you.

/** Slots in uTouchPoints. Mirrors TouchField.MAX_POINTS; changing one alone breaks the upload. */
#define TOUCH_MAX_POINTS 5

/** xy = the primary point, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchAnchor;
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots of uTouchPoints, including slots still fading after release. */
uniform int uTouchCount;
/** TouchField gesture: 0 none, 1 anchor, 2 axis, 3 vortex. */
uniform int uTouchGesture;
/** Two-finger vector, point 1 minus point 0. Zero with fewer than two down. */
uniform vec2 uTouchAxis;
/** Three-plus-finger signed swirl rate about their centroid. */
uniform float uTouchSpin;

/**
 * The user's Detail control as a march-step budget - MarchBudget.forDetail().
 *
 * A float rather than an int because it is a BUDGET, not a loop bound: the
 * loop bound is a compile-time constant (see lib_sdf3's RAYMARCH note) and
 * this is what the body breaks on. Uploaded to every fragment style; the ones
 * that do not march never read it and the linker drops it.
 */
uniform float uSteps;

float aband(float x) { return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.25)).r; }
float awave(float x) { return texture(uAudioTex, vec2(clamp(x, 0.0, 1.0), 0.75)).r; }

vec2 view() {
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= uResolution.x / uResolution.y;
    if (uPixelate > 0.001) {
        float px = mix(1.0, 12.0, uPixelate) * 24.0;
        uv = floor(uv * px) / px;
    }
    if (uMirrorX > 0.5) uv.x = abs(uv.x);
    // Kaleidoscope: fold the plane into uSymmetry angular wedges.
    if (uKaleido > 0.5 && uSymmetry >= 2.0) {
        float ang = atan(uv.y, uv.x);
        float rad = length(uv);
        float seg = 6.2831853 / uSymmetry;
        ang = abs(mod(ang, seg) - seg * 0.5);
        uv = vec2(cos(ang), sin(ang)) * rad;
    }
    // Drift ping-pongs rather than running away. The composite pass wraps its
    // own drift with fract() because it samples a BOUNDED image ("Wrap so the
    // image scrolls instead of smearing at the clamped edge"), but uv here
    // indexes an unbounded procedural domain, where a hard wrap would teleport
    // the whole field by a screen width once per cycle. A triangle wave is the
    // bounded form that stays continuous: its slope is exactly the old one for
    // the first cycle, so nothing pops on the styles that read as scrolling
    // (plasma, aurora, voronoi, grid, waves), while a centred subject - the
    // sun, the ring, the spiral - always comes back instead of leaving frame
    // for good and stranding the user on a black screen.
    vec2 driftPhase = fract(vec2(uDriftX, uDriftY) * uTime * 0.025 + 0.25);
    uv += 1.0 - 2.0 * abs(2.0 * driftPhase - 1.0);
    uv += uShake * uBeat * 0.03 * vec2(sin(uTime * 91.7), cos(uTime * 77.3));
    // Morph: blend the plane toward a polar remap (angle,radius swap), a
    // smooth geometric metamorphosis that works on any scene.
    if (uMorph > 0.001) {
        float mr = length(uv);
        float ma = atan(uv.y, uv.x);
        vec2 polar = vec2(ma / 3.14159, (mr - 0.7) * 1.6);
        uv = mix(uv, polar, uMorph * (0.6 + 0.15 * sin(uTime * 0.31)));
    }
    float a = uRotation + uSway * 0.35 * sin(uTime * 0.7);
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Beat-locked pulse: peaks exactly on the musical beat (uBeatPhase=0), and
    // rides the beat ENVELOPE so it is zero between hits. The phase clock in
    // ShaderScene free-runs at the last detected tempo, so without the
    // envelope the frame kept breathing once a beat through silence; every
    // other family gets this slider as CompositeGrade.pulseAmount (the slider
    // times the SQUARED envelope), and one slider has to mean one thing.
    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(6.2831853 * uBeatPhase), 2.0);
    float pulse = 1.0 + uPulse * 0.22 * beatEnv * beatEnv * beatBump;
    // Triangle-wave exponent: 1x -> 2x -> 1x smoothly, so the endless-zoom
    // phase wrap never causes a visible scale pop (2^1 snapping to 2^0). The
    // milkdrop post pass (pm_post_frag) already spells it this way; a sawtooth
    // exponent halved the magnification once per cycle on every scene shader.
    float z = uZoom * pulse * pow(2.0, 1.0 - abs(2.0 * uZoomPhase - 1.0)) * (1.0 + uBeat * uBeatResponse * 0.15);
    uv /= max(z, 0.05);
    uv += uTurbulence * 0.06 * vec2(sin(uv.y * 6.0 + uTime), cos(uv.x * 6.0 + uTime * 1.3));
    // Radial twist: rotate by an angle growing with radius.
    if (abs(uTwist) > 0.001) {
        float tr = length(uv) * uTwist * 2.0;
        uv = mat2(cos(tr), -sin(tr), sin(tr), cos(tr)) * uv;
    }
    // Tiling: repeat the plane into a uTile x uTile grid.
    if (uTile > 1.01) {
        uv = mod(uv * uTile * 0.5 + 1.0, 2.0) - 1.0;
    }
    // Domain warp: swirl coordinates by a sin/cos field.
    if (uWarp > 0.001) {
        float w = uWarp * 0.5;
        uv += w * vec2(sin(uv.y * 3.0 + uTime * 1.1), cos(uv.x * 3.0 + uTime * 0.9));
    }
    // Concentric ripple distortion driven by radius.
    if (uRipple > 0.001) {
        float r = length(uv);
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + uBass * 4.0);
    }
    return uv;
}
