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
    col += uFlash * uBeat * 0.6;
    col = col * uBright * uIntensity;
    return mix(col, max(vec3(1.0) - col, 0.0), uInvert);
}

// WINTER: a frozen pond seen from above - interference ripples travelling
// across the ice, voronoi frost crystals glinting on it, and three parallax
// layers of snow falling over the whole thing.
//
// Recovered from an abandoned branch and rebuilt for the current engine. Two
// things the original did are deliberately gone:
//
//  - It carried its own byte-copy of `pal()`. That is the drift the include
//    system exists to end, so the shared library is included instead and the
//    style gets the cyclic colour maps for free.
//  - It sampled `uFlow` itself to smear every layer by the FlowField. The
//    composite pass already warps EVERY style through that same field
//    (`composite_frag`'s `uFlowStrength` block), so a second read here would
//    apply one velocity field twice - the double-rotation bug seen in
//    another costume. The layered "blown around" motion is re-expressed with
//    what the scene is actually given: per-layer wind from Sway/Turbulence
//    and the mid band, which parallaxes exactly like the smear did and still
//    responds to a finger, because the finger reaches the composite warp.
float wHash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 wHash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

void main() {
    vec2 p = view();
    float t = uTime;
    // Wind: the layer displacement the smear field used to provide. Sway and
    // Turbulence steer it, the mids push it, and every layer takes a
    // different share of it - which is what makes the snow read as depth.
    vec2 wind = vec2(
        sin(t * 0.23) * (0.35 + uSway) + uTurbulence * 0.5 * sin(t * 0.61),
        cos(t * 0.17) * 0.18 * (0.3 + uMid)
    );

    // ---- icy water: interference ripples from three drifting sources ----
    vec2 c1 = vec2(sin(t * 0.30), cos(t * 0.23)) * 0.55;
    vec2 c2 = vec2(cos(t * 0.19 + 2.1), sin(t * 0.27 + 1.3)) * 0.65;
    float r0 = length(p);
    float r1 = length(p - c1);
    float r2 = length(p - c2);
    float wave = sin(r0 * (18.0 + uBass * 10.0) - t * 3.2) * exp(-r0 * 1.2);
    wave += sin(r1 * 24.0 - t * 4.1) * exp(-r1 * 1.8) * (0.5 + uMid * 1.6);
    wave += sin(r2 * 30.0 - t * 5.0) * exp(-r2 * 2.2) * (0.3 + uTreble * 1.4);
    // Beat rings skate outward across the ice, on the BPM-locked phase clock
    // so they land with the music rather than free-running. The ring fades as
    // it expands: without that, the phase wrapping 1 -> 0 teleports a lit
    // annulus from the corners back to the centre once per beat, which at a
    // fast tempo is a large-area luminance step - the thing VisualSafety's
    // budget exists to bound, arriving from a path the budget cannot see.
    float ringPhase = fract(uBeatPhase);
    float ring =
        exp(-abs(r0 - ringPhase * 1.7) * 12.0) * (1.0 - ringPhase) * uBeat * uBeatResponse;
    // Cool water body, brightened along wave crests (specular shimmer).
    float crest = pow(clamp(wave * 0.5 + 0.5, 0.0, 1.0), 3.0);
    vec3 col = pal(0.15 + wave * 0.25) * (0.16 + 0.30 * crest + uEnergy * 0.35);
    col += pal(0.85) * ring * 1.2;

    // ---- frost crystals: voronoi cracks glinting with treble ----
    vec2 fq = p * 5.0 - wind * 0.35;
    vec2 fcell = floor(fq);
    float f1 = 8.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = wHash22(fcell + g);
            o = 0.5 + 0.4 * sin(t * 0.4 + 6.2831 * o);
            float d = length(g + o - fract(fq));
            f1 = min(f1, d);
        }
    }
    float crack = pow(1.0 - clamp(f1, 0.0, 1.0), 6.0);
    col += pal(0.55) * crack * (0.12 + uTreble * 0.9 + uBeat * uBeatResponse * 0.25);

    // ---- snowfall: three parallax particle layers riding the wind ----
    float snow = 0.0;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float depth = 1.0 - fi * 0.28;
        float scale = 7.0 + fi * 6.0;
        // Fall + drift; nearer layers take more wind than far ones, which is
        // the parallax the old smear read produced.
        vec2 q = p * scale;
        q.y += t * (0.9 + fi * 0.5) * scale * 0.16;
        q.x += sin(t * (0.5 + fi * 0.2) + fi * 1.7) * (0.5 + uSway) * 1.2;
        q -= wind * scale * (0.55 + 0.35 * depth);
        vec2 cell = floor(q);
        vec2 jitter = wHash22(cell);
        vec2 fpos = fract(q) - (0.25 + 0.5 * jitter);
        float seed = wHash12(cell * 1.63 + fi * 9.7);
        float size = (0.045 + 0.06 * seed) * (1.0 + uBass * uBeatResponse * 0.4);
        float flake = smoothstep(size, size * 0.25, length(fpos));
        // Treble makes flakes twinkle; each flake blinks on its own phase, so
        // the twinkle is per-flake texture and never a full-frame flicker.
        float twinkle = 0.65 + 0.35 * sin(t * (2.0 + seed * 5.0) + seed * 40.0 + uTreble * 6.0);
        snow += flake * twinkle * depth * (0.55 + 0.45 * step(0.25, seed));
    }
    // Snow stays near-white with a hint of the palette so it reads as snow
    // on any colour scheme.
    col += snow * mix(vec3(0.95), pal(0.6), 0.25) * (0.8 + uEnergy * 0.5);

    // Spectrum shimmer along the bottom "shoreline" - a quiet nod to the
    // audio without a literal bar display.
    float shore = smoothstep(0.15, -0.6, p.y);
    col += pal(0.35) * shore * aband(vUv.x) * 0.35;

    fragColor = vec4(grade(col), 1.0);
}
