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

// LAVA: a lava lamp with a real lifecycle. Every blob owns a PERMANENT home
// lane (its x position never relocates), rises and sinks on its own 18-40 s
// clock, and is born and dies IN PLACE through a smooth grow/dissolve
// envelope - so the composition stays anchored and shapes appear, merge and
// vanish by morphing like wax, never by popping in. Noise-wobbled edges make
// the surfaces undulate like smoke underwater.
//
// Recovered from an abandoned branch and rebuilt for the current engine:
//
//  - It carried its own byte-copy of `pal()`; the shared library is included
//    instead, which is also what gives the style the cyclic colour maps.
//  - Its `view()` predated the endless-zoom rewrite and still spelled the
//    magnification as `pow(2.0, 1.0 - abs(2.0 * uZoomPhase - 1.0))`, a
//    triangle that zoomed in and then back OUT. Every current style uses the
//    canonical prelude's `pow(2.0, uZoomPhase)`, which is the continuous
//    octave ramp `ShaderScene` wraps at 1 - so Endless zoom now looks the
//    same here as everywhere else.
//  - Treble drove nothing at all. It steers the surface wobble now, which is
//    the fastest-moving thing on a blob and the only part of the look that
//    could carry a high band without turning into flicker.
float lavaHash(float n) { return fract(sin(n * 78.233) * 43758.5453); }

void main() {
    vec2 p = view();
    float t = uTime * 0.55;
    float field = 0.0;
    float hueAcc = 0.0;

    // The molten pool at the bottom every blob is born from and returns to.
    {
        vec2 q = vec2(p.x * 0.42, p.y + 1.52 - uBass * 0.06);
        // Slow traveling surface wave so the pool breathes like liquid.
        q.y += 0.05 * sin(p.x * 2.6 + t * 0.5) * sin(t * 0.23);
        float contrib = 0.34 / max(dot(q, q), 0.02);
        field += contrib;
        hueAcc += 0.08 * contrib;
    }

    for (int i = 0; i < 10; i++) {
        float fi = float(i) / 10.0;
        float seed = lavaHash(fi + 0.37);
        float seed2 = lavaHash(fi * 3.1 + 9.2);
        // Own slow clock: one full rise-and-sink takes 18-40 s, cosine ease
        // so it detaches and lands gently.
        float period = mix(18.0, 40.0, seed);
        float u = fract(t / period + seed2 * 1.7);
        float yc = -1.30 + 2.55 * (0.5 - 0.5 * cos(6.2831853 * u));
        // PERMANENT home lane: x0 depends only on the blob's identity, so a
        // blob always works the same column - it wanders around home but
        // never teleports somewhere new.
        float x0 = (seed2 * 2.0 - 1.0) * 0.9;
        float x = x0 +
            0.16 * sin(t * (0.15 + seed * 0.25) + seed * 31.0) +
            uMid * 0.12 * sin(t * 0.8 + seed * 9.0);
        // Life envelope: each blob periodically dissolves away and later
        // regrows IN ITS OWN LANE (radius -> 0 and back over ~12 s), so the
        // lamp keeps adding and removing wax without anything popping.
        float lifePeriod = mix(45.0, 90.0, lavaHash(seed * 5.21));
        float lp = fract(t / lifePeriod + seed * 2.3);
        float life = smoothstep(0.0, 0.14, lp) * (1.0 - smoothstep(0.82, 1.0, lp));
        // Bass makes the wax breathe; each blob listens to its own band.
        float r = mix(0.13, 0.24, lavaHash(seed * 7.13)) *
            (0.15 + 0.85 * life) *
            (1.0 + 0.30 * aband(fi) * uBeatResponse + 0.10 * uBass);
        vec2 q = p - vec2(x, yc);
        // Rising/sinking blobs stretch vertically like real wax.
        float vy = abs(sin(6.2831853 * u));
        q.y *= mix(0.95, 0.62, vy);
        // Smoke-like surface: the blob's edge undulates with a slow
        // two-frequency wobble instead of staying a perfect ellipse. Treble
        // rides the wobble's depth - it moves the OUTLINE, so a bright hi-hat
        // makes the wax shiver without changing the frame's luminance.
        float wob = 0.22 + 0.10 * uTreble;
        q += r * wob * vec2(
            sin(q.y * 9.0 + t * 0.9 + seed * 13.0) + 0.5 * sin(q.y * 17.0 - t * 0.6),
            cos(q.x * 8.0 + t * 0.7 + seed * 7.0)
        );
        float contrib = r * r / max(dot(q, q), 0.0008);
        field += contrib * life;
        hueAcc += (0.15 + 0.7 * fi) * contrib * life;
    }

    float hue = hueAcc / max(field, 0.001);
    float body = smoothstep(0.95, 1.25, field);
    float core = smoothstep(1.5, 3.0, field);
    float rim = smoothstep(0.95, 1.06, field) - smoothstep(1.18, 1.5, field);
    vec3 col = pal(hue) * body * (0.55 + 0.45 * uEnergy);
    col += pal(hue + 0.13) * core * 0.55;
    // The beat lands on the RIM only - a thin contour band, never the whole
    // frame - so the strongest transient in the style is a highlight on an
    // edge rather than a luminance step the flash limits would have to bound.
    col += vec3(1.0, 0.92, 0.75) * max(rim, 0.0) * (0.22 + uBeat * uBeatResponse * 0.35);
    // Faint lamp-glass ambience so the empty space isn't dead black.
    col += pal(0.85) * (1.0 - body) * 0.045 * (1.2 - p.y * 0.6);
    fragColor = vec4(grade(col), 1.0);
}
