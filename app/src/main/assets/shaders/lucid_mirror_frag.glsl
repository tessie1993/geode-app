#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_lucid

// Lucid Mirror: the inside of a three-mirror kaleidoscope tube. Where the
// family's other kaleidoscope (Mandala) folds around a centre, this one has
// no centre: the plane is folded by a triangle wave along three directions
// sixty degrees apart, twice over, which is what three mirrors set at a
// triangle do to whatever is between them - an endless lattice of reflected
// triangles in every direction. The object in the tube is a value nest three
// levels deep with a sine lattice laid over it, so there are both soft blobs
// and crisp woven threads to reflect.
//
// Coloured as light through a prism, in pastel: the nest's value walks the
// palette like a spectrum, washed toward white, and the mirror seams are
// drawn as thin dark lines so the tube's glass is legible.
//
// ---- audio ------------------------------------------------------------------
//
// Every ONSET turns the tube a step: the plane's rotation is
// (SND_ONSET_COUNT - SND_ONSET_ENV) * 0.37 rad, which is continuous across the hit (the
// count steps up exactly as the envelope jumps to 1) and then eases into the
// new angle as the 0.22 s envelope decays - a twist of the barrel per note,
// never a snap. The object inside lurches with lucidBump of the onset.
// sndShaped(SND_HIGH) sharpens the woven threads. Brightness follows SND_ENERGY_TREND gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the drift of the object in the tube.
// Complexity: uMorph shrinks the mirror triangles (more reflections) and,
// past 0.5, adds a nest level and an octave. Chaos: uTurbulence/uWarp raise
// warp strength and jitter the object; uTwist twists the plane. Color: the
// spectrum is pal(). Energy: uIntensity scales the onset lurch (the barrel
// step itself is fixed, so moving the slider never spins the tube). Space:
// uZoom/uRotation through lucidView. The user's kaleidoscope is skipped: the
// tube is its own.

/** Triangle-wave folds along three directions 60 degrees apart. Returns the folded point; `seam` gets the distance to the nearest mirror. */
vec2 mirrorBox(vec2 p, float h, out float seam) {
    seam = h;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 3; i++) {
            float ang = float(i) * LUCID_TAU / 3.0 + 0.5 * LUCID_PI;
            vec2 n = vec2(cos(ang), sin(ang));
            float d = dot(p, n);
            float fd = h - abs(mod(d, 2.0 * h) - h);
            p += n * (fd - d);
            seam = min(seam, min(fd, h - fd));
        }
    }
    return p;
}

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float treb = clamp(sndShaped(SND_HIGH), 0.0, 1.5);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    float barrel = mod((SND_ONSET_COUNT - clamp(SND_ONSET_ENV, 0.0, 1.0)) * 0.37, LUCID_TAU);
    vec2 p = lucidRot(barrel + t * 0.015) * uv;
    float h = 0.55 - 0.2 * clamp(uMorph, 0.0, 1.0);
    float seam;
    vec2 f = mirrorBox(p, h, seam);

    float lurch = lucidBump(SND_ONSET_ENV) * 0.35 * uIntensity;
    vec2 q = f * 2.4 + vec2(t * 0.07 + flow * 0.1, -t * 0.05) + vec2(lurch, -0.6 * lurch);
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.3 * lucidChaos(), t * 0.22, LUCID_VALUE, oct);
    float threads = lucidSines((q + n.xy * 0.8) * 3.2);
    float weave = smoothstep(0.75 - 0.15 * clamp(treb, 0.0, 1.0), 1.0, abs(threads) * 1.4);

    float s = 0.5 + 0.5 * n.z;
    vec3 spectrum = pal(s * 1.4 + 0.25 * n.w);
    vec3 col = mix(spectrum, vec3(1.0), 0.32) * (0.35 + 0.65 * smoothstep(0.1, 0.9, s));
    col += pal(s + 0.5) * weave * 0.35;
    col = lucidChannelTurn(col, 0.6 * n.w);
    col = lucidCubic(col, 0.5);
    // The glass: thin dark seams where the mirrors meet.
    col *= mix(0.25, 1.0, smoothstep(0.0, 0.025, seam));

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
