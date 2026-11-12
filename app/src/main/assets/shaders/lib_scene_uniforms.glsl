// The uniform block, the sound engine's frame (lib_sound, included below) and
// view() that EVERY fragment style shares.
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
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized. Declared here as well as at the
// top of the style file so an including shader that forgot its own precision
// statement still reads full-precision texels.
precision highp sampler2D;

// Sound reaches every style through here, so a style never includes lib_sound
// itself (a second paste is a redefinition error). Only a style that does not
// include this file includes lib_sound directly.
//#include lib_sound

uniform float uTime;
uniform vec2 uResolution;
uniform float uZoom;
uniform float uRotation;
uniform float uColorShift;
uniform float uHueRange;
uniform float uSat;
uniform float uBright;
uniform float uInvert;
uniform float uIntensity;
uniform float uMirrorX;
uniform float uTurbulence;
uniform float uPalBase;
uniform float uPalRange;
uniform float uContrast;
uniform float uGamma;
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
uniform float uDriftX;
uniform float uDriftY;
uniform float uTile;
uniform float uTwist;
uniform float uTemperature;
uniform float uSolarize;

// ---- the style knobs --------------------------------------------------------
//
// Six per-style controls, each 0..1, whose meaning each style names in its own
// header in the same order: k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD,
// k5 SPACE. They live here rather than in the style files because they are one
// contract for every style that reads them: the engine fills one array, and a
// second declaration in a style would be a redefinition error once pasted
// beside this one. A style that never reads them links the array away, so it
// costs the others nothing.
//
// With Settings › Behaviour › Note & key reactions on, core/viz/NoteResponse
// drives them by meaning, each from several fields of the sound frame and
// smoothed at a musical rate: k0 PULSE the drums, weighted by how percussive
// the track is; k1 TEXTURE timbre (brightness, density, noise); k2 FLOW
// sustained energy; k3 TONE the melody's height; k4 BUILD tension and the
// feeling's energy; k5 SPACE interval motion and stereo width. Off, every knob
// is exactly 0.5 (the midpoint each style was tuned around). Either way a
// style must read a knob as a continuous value and never as a trigger.
uniform float uKnob[6];

// ---- where the fingers are ------------------------------------------------
//
// core/viz/TouchField is the model; ShaderScene.uploadTouch() is the
// upload. All of it is ZERO when nothing is being touched, so a style that
// ignores these draws exactly what it drew before they existed.
//
// COORDINATES: y-up NDC. x and y in -1..1, origin at the centre of the
// surface, +y toward the top. Aspect is NOT pre-applied - multiply x by
// uResolution.x/uResolution.y yourself, exactly as view() does on its first
// two lines, or a finger on a landscape phone lands somewhere the user did
// not put it. lib_touch.glsl does that correction for you.

/** Slots in uTouchPoints. Mirrors TouchField::kMaxPoints (core/viz); changing one alone breaks the upload. */
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

/**
 * The Formula param, 0..1: which of a style's three formulas draws its
 * geometry (0, 0.5 and 1 each one; lib_strata's strataFormula crossfades
 * between neighbours). The styles that have no formulas never read it.
 */
uniform float uFormula;

vec2 view() {
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= uResolution.x / uResolution.y;
    if (uPixelate > 0.001) {
        float px = mix(1.0, 12.0, uPixelate) * 24.0;
        uv = floor(uv * px) / px;
    }
    if (uMirrorX > 0.5) uv.x = abs(uv.x);
    // VIEW_BENDS_IN_3D: a present that bends its 3D shape by the Kaleidoscope, Twist, Tile and Ripple
    // (lib_field3d) defines it before including this file, so the screen is not bent a second time.
#ifndef VIEW_BENDS_IN_3D
    // Kaleidoscope: fold the plane into uSymmetry angular wedges.
    if (uKaleido > 0.5 && uSymmetry >= 2.0) {
        float ang = atan(uv.y, uv.x);
        float rad = length(uv);
        float seg = 6.2831853 / uSymmetry;
        ang = abs(mod(ang, seg) - seg * 0.5);
        uv = vec2(cos(ang), sin(ang)) * rad;
    }
#endif
    // VIEW_OWNS_SHAPE: a style that spends uMorph, uTurbulence and uWarp on
    // its own geometry (the style families: Complexity and Chaos macros move
    // these) defines it before including this file, so the generic screen
    // distortions below do not bend its picture a second time.
#ifndef VIEW_OWNS_SHAPE
    // Morph: blend the plane toward a polar remap (angle,radius swap), a
    // smooth geometric metamorphosis that works on any scene.
    // The angle is folded (abs) rather than taken raw: atan jumps from +pi to
    // -pi along the negative x axis, so a raw ma / pi tore every morphing
    // frame from the left edge to the centre, and MotionField keeps morph
    // above zero for most music.
    if (uMorph > 0.001) {
        float mr = length(uv);
        float ma = mr > 1e-5 ? atan(uv.y, uv.x) : 0.0; // atan(0, 0) is undefined in GLSL ES
        vec2 polar = vec2(abs(ma) / 1.5707963 - 1.0, (mr - 0.7) * 1.6);
        // Faded to nothing at the centre: atan has no angle at r = 0, so every
        // angle met at one point and the centre of the frame drew as a pinched
        // vortex whenever morph was up (MotionField holds it near 0.7).
        uv = mix(uv, polar, uMorph * (0.6 + 0.15 * sin(uTime * 0.31)) * smoothstep(0.0, 0.6, mr));
    }
#endif
    float a = uRotation + uSway * 0.35 * sin(uTime * 0.7);
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Drift and shake move the picture after the spin and the morph. Added
    // before them, the rotation pivoted on -drift and the morph's pinch rode
    // the drift, so the whole frame spun around a wandering off-centre point.
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
    float z = uZoom;
    uv /= max(z, 0.05);
#ifndef VIEW_OWNS_SHAPE
    uv += uTurbulence * 0.06 * vec2(sin(uv.y * 6.0 + uTime), cos(uv.x * 6.0 + uTime * 1.3));
#endif
#ifndef VIEW_BENDS_IN_3D
    // Radial twist: rotate by an angle growing with radius.
    if (abs(uTwist) > 0.001) {
        float tr = length(uv) * uTwist * 2.0;
        uv = mat2(cos(tr), -sin(tr), sin(tr), cos(tr)) * uv;
    }
    // Tiling: repeat the plane into a uTile x uTile grid, every other tile
    // mirrored so neighbours meet edge to edge. A hard mod() wrap cracked the
    // picture along every tile boundary, and the macros drive uTile live.
    if (uTile > 1.01) {
        uv = 1.0 - abs(mod(uv * uTile * 0.5 + 1.0, 4.0) - 2.0);
    }
#endif
#ifndef VIEW_OWNS_SHAPE
    // Domain warp: swirl coordinates by a sin/cos field.
    if (uWarp > 0.001) {
        float w = uWarp * 0.5;
        uv += w * vec2(sin(uv.y * 3.0 + uTime * 1.1), cos(uv.x * 3.0 + uTime * 0.9));
    }
#endif
#ifndef VIEW_BENDS_IN_3D
    // Concentric ripple distortion driven by radius.
    if (uRipple > 0.001) {
        float r = length(uv);
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndShaped(SND_LOW) * 4.0);
    }
#endif
    return uv;
}
