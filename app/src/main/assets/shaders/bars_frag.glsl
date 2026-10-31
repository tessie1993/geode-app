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
uniform float uZoomPhase;
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
        float ma = atan(uv.y, uv.x);
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
    // Triangle-wave exponent: 1x -> 2x -> 1x smoothly, so the endless-zoom
    // phase wrap never causes a visible scale pop (2^1 snapping to 2^0). The
    // milkdrop post pass (pm_post_frag) already spells it this way; a sawtooth
    // exponent halved the magnification once per cycle on every scene shader.
    float z = uZoom * pow(2.0, 1.0 - abs(2.0 * uZoomPhase - 1.0));
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
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndLevel(SND_BASS_ATT) * 4.0);
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

// Spectrum analyzer bars with glow, standing on a reflecting floor.
//
// What reacts to what (sound events via lib_sound; camera motion is the
// composite's, applied to this picture automatically):
//   - each bar's height is its analyser band, as before.
//   - every bar wears a peak cap. On each beat (SND_KICK_TOGGLE) the caps are
//     thrown up above their bars, by as much as their band region
//     (vec4(sndSpectrum(0.10), sndLevel(SND_BASS_ATT), sndSpectrum(0.41), sndLevel(SND_MID_ATT))/vec4(sndSpectrum(0.70), sndShaped(SND_HIGH), sndSpectrum(0.93), sndLevel(SND_FLUX))) is loud, and fall back onto them as the beat
//     envelope decays - accelerating, like something dropped.
//   - the snare (SND_SNARE_ENV) tilts the row into a travelling wave: bars, caps
//     and their reflections ride up and down it. Position only.
//   - the floor reflects the whole row, dimmed and fading with depth - a
//     fixed fraction of the bars above it, never driven by a hit.

/** Bar count across the screen. */
#define BARS_N 48.0
/** The floor line, in view() units. */
#define BARS_FLOOR -0.45
/** Bar height per unit of band level. */
#define BARS_GAIN 1.1
/** How far a beat throws a cap above its bar, at full region level. */
#define BARS_CAP_THROW 0.3
/** Cap thickness. */
#define BARS_CAP_H 0.018
/** Reflection strength and how fast it fades below the floor. */
#define BARS_REFLECT 0.35
#define BARS_REFLECT_FADE 3.5

/**
 * The band region a bar position 0..1 falls in, read from the sound frame:
 * viz/Reactions' seven regions over the analyser's 64 bands (boundaries at
 * bands 8, 16, 26, 36, 46 and 55).
 */
float barsRegion(float x) {
    if (x < 0.125) return sndSpectrum(0.10);
    if (x < 0.25) return sndLevel(SND_BASS_ATT);
    if (x < 0.40625) return sndSpectrum(0.41);
    if (x < 0.5625) return sndLevel(SND_MID_ATT);
    if (x < 0.71875) return sndSpectrum(0.70);
    if (x < 0.859375) return sndShaped(SND_HIGH);
    return sndSpectrum(0.93);
}

/** One bar column at height y above its floor: body, glow and cap. */
vec3 barsColumn(float seg, float xf, float y, float h, float cap) {
    float bar = step(0.0, y) * step(y, h) * step(xf, 0.8);
    float glow = exp(-max(y - h, 0.0) * 9.0) * 0.5 * step(0.0, y);
    float cells = step(fract(y * 24.0), 0.8);
    float capped = step(cap, y) * step(y, cap + BARS_CAP_H) * step(xf, 0.8);
    return pal(seg) * (bar * cells * (0.4 + h) + glow * h) + pal(seg + 0.12) * capped * 0.9;
}

void main() {
    vec2 uv = view();
    float x = clamp(uv.x * 0.5 + 0.5, 0.0, 1.0);
    float seg = floor(x * BARS_N) / BARS_N;
    float xf = fract(x * BARS_N);
    float h = (0.5 * sndEq(seg)) * 0.6 * BARS_GAIN;

    // The cap: thrown up on the beat and falling back with gravity's shape -
    // 1 - (1 - e)^2 holds near the top and then drops faster and faster.
    float beat = clamp(SND_KICK_TOGGLE, 0.0, 1.0);
    float fall = 1.0 - (1.0 - beat) * (1.0 - beat);
    float cap = h + 0.012 + BARS_CAP_THROW * fall * barsRegion(seg);

    // The snare's wave: a vertical offset per bar, travelling along the row.
    float tilt = 0.18 * clamp(SND_SNARE_ENV, 0.0, 1.0) * sin(seg * 9.0 - uTime * 3.0);
    float floorY = BARS_FLOOR + tilt;

    vec3 col;
    if (uv.y >= floorY) {
        col = barsColumn(seg, xf, uv.y - floorY, h, cap);
    } else {
        // The reflection: the same column mirrored in the floor, dimmed and
        // fading with depth.
        float depth = floorY - uv.y;
        col = barsColumn(seg, xf, depth, h, cap) * BARS_REFLECT * exp(-depth * BARS_REFLECT_FADE);
    }
    // The row spans uv.x -1..1. Past it x is clamped to an edge bar with
    // xf = 0, which drew that bar as one solid slab across the rest of a
    // landscape frame (16:9 export, wallpaper).
    col *= step(abs(uv.x), 1.0);
    fragColor = vec4(grade(col), 1.0);
}
