#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

uniform float uTime;
uniform vec2 uResolution;
uniform float uBass;
uniform float uMid;
uniform float uTreble;
uniform float uEnergy;
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
uniform float uBeatPhase;
uniform float uDriftX;
uniform float uDriftY;
uniform float uTile;
uniform float uTwist;
uniform float uTemperature;
uniform float uSolarize;

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
    float a = uRotation + uSway * 0.35 * sin(uTime * 0.7);
    uv = mat2(cos(a), -sin(a), sin(a), cos(a)) * uv;
    // Wave three: the beat-locked pulse and beat-response zoom widen are
    // gone (they were flash/spike behaviour). Endless-zoom keeps its
    // triangle-wave exponent (1x -> 2x -> 1x) so the phase wrap never pops.
    float z = uZoom * pow(2.0, 1.0 - abs(2.0 * uZoomPhase - 1.0));
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

//#include lib_scene_motion
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

// motion: uBassRel -> winding tightness, uEnergyRel -> arm brightness
// Log-spiral arms winding tighter with the bass.
void main() {
    vec2 uv = view();
    float r = length(uv) + 1e-4;
    float ang = atan(uv.y, uv.x);
    float arms = 2.0 + floor(uMorph * 4.0);
    float windRel = clamp(uBassRel - 1.0, -1.0, 1.0);
    float sp = ang * arms + log(r) * (5.0 + windRel * 1.5) - uTime * 1.8;
    float v = 0.5 + 0.5 * sin(sp);
    v = pow(v, 3.0 - uEnergy * 1.6);
    float fade = exp(-r * 0.8);
    float glowRel = clamp(uEnergyRel - 1.0, -1.0, 1.0);
    vec3 col = pal(fract(ang / 6.2831853 + r * 0.25)) * v * (0.3 + uEnergy + glowRel * 0.15) * (0.35 + fade);
    col += pal(0.5) * exp(-r * 6.0) * (0.4 + uBass);
    fragColor = vec4(grade(col), 1.0);
}
