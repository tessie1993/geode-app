#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

uniform float uTime;
uniform vec2 uResolution;
uniform float uSpeed;
uniform float uZoom;
uniform float uRotation;
//#include lib_zoom
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
uniform float uDriftX;
uniform float uDriftY;
uniform float uShake;
uniform float uTile;
uniform float uTwist;
uniform float uTemperature;
uniform float uSolarize;
uniform float uFlash;

//#include lib_sound


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
    // Morph: blend the plane toward a polar remap (angle,radius swap), a
    // smooth geometric metamorphosis that works on any scene.
    if (uMorph > 0.001) {
        float mr = length(uv);
        float ma = mr > 1e-5 ? atan(uv.y, uv.x) : 0.0; // atan(0, 0) is undefined in GLSL ES
        vec2 polar = vec2(abs(ma) / 1.5707963 - 1.0, (mr - 0.7) * 1.6);
        // Faded to nothing at the centre: atan has no angle at r = 0, so every
        // angle met at one point and the centre of the frame drew as a pinched
        // vortex whenever morph was up (MotionField holds it near 0.7).
        uv = mix(uv, polar, uMorph * (0.6 + 0.15 * sin(uTime * 0.31)) * smoothstep(0.0, 0.6, mr));
    }
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
    // Endless zoom: this layer's magnification (lib_zoom draws two, an octave apart).
    gZoomRead = true;
    float z = uZoom * gZoomMag;
    uv /= max(z, 0.05);
    uv += uTurbulence * 0.06 * vec2(sin(uv.y * 6.0 + uTime), cos(uv.x * 6.0 + uTime * 1.3));
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
    // Domain warp: swirl coordinates by a sin/cos field.
    if (uWarp > 0.001) {
        float w = uWarp * 0.5;
        uv += w * vec2(sin(uv.y * 3.0 + uTime * 1.1), cos(uv.x * 3.0 + uTime * 0.9));
    }
    // Concentric ripple distortion driven by radius.
    if (uRipple > 0.001) {
        float r = length(uv);
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndShaped(SND_LOW) * 4.0);
    }
    return uv;
}

//#include lib_palette

vec3 grade(vec3 col) {
    if (uBloom > 0.001) col += uBloom * col * col;
    if (uPosterize > 0.001) {
        float levels = mix(24.0, 3.0, uPosterize);
        col = floor(col * levels + 0.5) / levels;
    }
    float g = dot(col, vec3(0.299, 0.587, 0.114));
    if (uDuotone > 0.5) col = pal(g);
    col = mix(vec3(g), col, uSat);
    col = (col - 0.5) * uContrast + 0.5;
    col = pow(max(col, 0.0), vec3(1.0 / max(uGamma, 0.05)));
    col.r += uTemperature * 0.12;
    col.b -= uTemperature * 0.12;
    if (uSolarize > 0.5) col = abs(1.0 - 2.0 * col);
    col = col * uBright * uIntensity;
    return mix(col, max(vec3(1.0) - col, 0.0), uInvert);
}

// Kaleidoscope: angular mirror folds over a flowing audio pattern.
//
// What reacts to what (sound events via lib_sound; camera motion is the
// composite's, applied to this picture automatically):
//   - a section change re-picks the fold count (4 to 9 wedges, hashed from
//     SND_CUT_COUNT) and crossfades the old count into the new one over the
//     section envelope (SND_HELD_HARD_CUT), then holds it until the next section.
//   - hats (SND_HAT_ENV) sparkle: a sparse, hashed scatter of points across the
//     folded plane, re-scattered on every onset, so the sparkles mirror into
//     every wedge. Local points only, never the frame.
//   - the pattern flows with the analyser bands and bass lifts it, as before.

/** Sparkle grid density across the folded plane. */
#define KAL_SPARKLE_CELLS 22.0

/** PCG integer hash: exact on every device, unlike a sin() hash. */
uint kalHash(uint h) {
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (h >> 22u) ^ h;
}

/** The fold count for section number `n`: 4..9. */
float kalFolds(float n) {
    return 4.0 + float(kalHash(uint(max(n, 0.0))) % 6u);
}

/** One kaleidoscope at `seg` wedges: the folded point and the pattern value at it. */
float kalPattern(vec2 uv, float seg, out vec2 folded) {
    float a = atan(uv.y, uv.x);
    float r = length(uv);
    a = abs(mod(a, 6.2831853 / seg) - 3.1415927 / seg);
    folded = vec2(cos(a), sin(a)) * r;
    return sin(folded.x * 7.0 + uTime) * cos(folded.y * 7.0 - uTime * 0.8)
        + (0.5 * sndEq(fract(r + uTime * 0.05))) * 1.2;
}

void main() {
    vec2 uv = view();
    float r = length(uv);

    // The section crossfade, without frame-to-frame state: when a section
    // lands, SND_CUT_COUNT steps and SND_HELD_HARD_CUT jumps to 1 on the same frame,
    // so the weight starts at the OLD count and eases to the new one as the
    // envelope falls. Both counts are drawn and mixed, so nothing pops.
    float w = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    w = w * w * (3.0 - 2.0 * w);
    vec2 foldOld;
    vec2 foldNew;
    float fOld = kalPattern(uv, kalFolds(SND_CUT_COUNT - 1.0), foldOld);
    float fNew = kalPattern(uv, kalFolds(SND_CUT_COUNT), foldNew);
    float f = mix(fOld, fNew, w);
    vec3 col = pal(f * 0.3 + r * 0.4) * (0.3 + f * 0.5 + sndShaped(SND_LOW) * 0.6);

    // Hat sparkles on the folded plane, so every wedge mirrors them. A cell
    // carries one when its hash, re-rolled per onset, clears the bar.
    vec2 folded = mix(foldOld, foldNew, w);
    vec2 cell = floor(folded * KAL_SPARKLE_CELLS);
    vec2 local = fract(folded * KAL_SPARKLE_CELLS) - 0.5;
    uvec2 c = uvec2(ivec2(cell));
    float pick = float(kalHash(c.x ^ kalHash(c.y ^ kalHash(uint(max(SND_ONSET_COUNT, 0.0))))) & 0xffffu) * (1.0 / 65536.0);
    float sparkle = step(0.94, pick) * (1.0 - smoothstep(0.0, 0.32, length(local))) * clamp(SND_HAT_ENV, 0.0, 1.0);
    col += pal(f * 0.3 + 0.5) * sparkle * 0.9;

    col *= smoothstep(1.6, 0.2, r);
    fragColor = vec4(grade(col), 1.0);
}
