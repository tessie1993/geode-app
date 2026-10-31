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
        uv *= 1.0 + uRipple * 0.15 * sin(r * 14.0 - uTime * 3.0 + sndLevel(SND_BASS) * 4.0);
    }
    return uv;
}

//#include lib_palette

/**
 * A hashed point in the unit disc for event number `n` (PCG integer hash, so
 * it is exact on every device). Used to pick where the next glide goes.
 */
vec2 hashSpot(float n) {
    uint h = uint(max(n, 0.0)) * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    float a = float(h & 0xffffu) * (6.2831853 / 65536.0);
    float r = sqrt(float(h >> 16u) * (1.0 / 65536.0));
    return vec2(cos(a), sin(a)) * r;
}

/**
 * The pitch-change glide, without frame-to-frame state.
 *
 * The SPOT is chosen by the onset count and eased from the previous count's
 * spot to the new one by the onset envelope: when an onset lands, floor(SND_HIT_TIME)
 * steps and SND_ONSET_ENV jumps to at least 0.4 on the same frame (viz/Reactions
 * triggers it at 0.4..1, not 1), so the mix, measured from 0.4, starts exactly
 * at the old spot and glides to the new one as the envelope falls - it moves
 * without ever stepping. The previous count is wrapped like floor(SND_HIT_TIME) itself
 * (4096), so the count's own wrap is not a jump either. HOW FAR the point travels toward the spot is the
 * pitch-change envelope shaped into a hump (4e(1-e)), which is 0 both before
 * the event (e = 0) and at its instant rise (e = 1), so the point eases out
 * to the spot and back over the envelope's fall rather than snapping there.
 */
vec2 pitchGlide() {
    float g = (1.0 - smoothstep(0.05, 0.4, clamp(SND_ONSET_ENV, 0.0, 1.0)));
    vec2 spot = mix(hashSpot(mod(floor(SND_HIT_TIME) - 1.0, 4096.0)), hashSpot(floor(SND_HIT_TIME)), g);
    float e = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    return spot * (4.0 * e * (1.0 - e));
}

//#include lib_scene_grade

// Audio-reactive Julia set; endless zoom dives into the fractal.
//
// What reacts to what (sound events via lib_sound; camera motion is the
// composite's, applied to this picture automatically):
//   - a pitch change (SND_HELD_NOTE_CHANGE) glides c out to a hashed spot and back, the
//     spot re-picked per onset (see pitchGlide), so the set reshapes itself
//     on every new note without ever snapping.
//   - an onset (SND_ONSET_ENV) gives the palette a brief hue phase step.
//   - the kick (SND_KICK_ENV) grows the zoom slightly.
//   - bass and mids still swing c, treble stretches the palette, and each
//     escape band is lit by its own analyser band.
void main() {
    // The kick grows the set by up to 6%: scale, not brightness.
    vec2 uv = view() * 1.4 / (1.0 + 0.06 * clamp(SND_KICK_ENV, 0.0, 1.0));
    // uTime is already the speed-INTEGRATED clock (ShaderScene.shaderTime), so
    // multiplying by uSpeed again made the orbit rate go as speed^2 and, worse,
    // reintroduced exactly the teleport that integrating exists to prevent: at
    // t = elapsed * speed, nudging the slider jumps the orbit by elapsed *
    // delta-speed, which after ten minutes is several radians and a different
    // fractal. No built-in scene shader reads uSpeed; it stays declared for the
    // in-app GLSL editor, where a user shader may want the raw slider value.
    float t = uTime * 0.15;
    vec2 c = vec2(-0.745 + 0.11 * cos(t) + sndLevel(SND_BASS) * 0.08, 0.186 + 0.11 * sin(t * 1.3) + sndShaped(SND_MIDDLE) * 0.05);
    // Small: c is the most sensitive parameter there is, and 0.07 already
    // turns one family of spirals into another.
    c += 0.07 * pitchGlide();
    vec2 z = uv;
    float iter = 0.0;
    const float maxIter = 64.0;
    for (float i = 0.0; i < maxIter; i++) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        if (dot(z, z) > 4.0) break;
        iter++;
    }
    float f = iter / maxIter;
    float hueStep = 0.12 * clamp(SND_ONSET_ENV, 0.0, 1.0);
    vec3 col = pal(f * (1.0 + sndLevel(SND_TREBLE_ATT)) + hueStep) * (0.25 + f * 1.2 + sndSpectrum(f) * 0.8);
    fragColor = vec4(grade(col), 1.0);
}
