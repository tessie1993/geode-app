#version 300 es
precision highp float;

// The note blooms (core/viz/NoteBloomPass): one paradoxical fractal flower per sounding note (lib_bloom),
// screened over the finished frame of every style, after the composite and before the user's overlay.
//
// NoteResponse decides everything per bloom on the CPU, deterministically (so an export equals the
// screen): where it is, how big, its hue, how open, how bright, its petals and depth, its turn. This pass
// only draws. It is scissored to the blooms' bounding box, and every pixel tests each bloom's disc before
// doing any work, so a pixel outside every flower costs eight distance tests and nothing else.
//
// Luminance safety: the screen blend (src + dst·(1 − src)) can only lighten toward white, never past it,
// and the output is capped at 0.8 so even a bloom over black never reaches full white; NoteResponse
// gives each bloom a 160 ms rise and dims new ones past three a second (VisualSafety's flash budget).

//#include lib_bloom

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform int uBloomCount;
// a = (x, y, radius, hue), b = (open, envelope, petals, depth), c = (angle, stretch, seed, tension).
uniform vec4 uBloomA[8];
uniform vec4 uBloomB[8];
uniform vec4 uBloomC[8];
// The last notes' colours, newest first.
uniform float uNoteHues[6];
// The switch's ease times the safety cap.
uniform float uBloomGain;

void main() {
    float shortSide = min(uResolution.x, uResolution.y);
    vec2 frag = gl_FragCoord.xy;
    float hues[6];
    for (int i = 0; i < 6; i++) hues[i] = uNoteHues[i];
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= uBloomCount) break;
        vec4 a = uBloomA[i];
        vec4 b = uBloomB[i];
        vec4 c = uBloomC[i];
        float radius = a.z * shortSide;
        float stretch = c.y;
        vec2 d = frag - a.xy * uResolution;
        // Early out: outside this bloom's disc (stretched up for high notes) there is nothing to draw.
        float reach = radius * max(stretch, 1.0);
        if (dot(d, d) > reach * reach) continue;
        vec2 p = d / radius;
        p.y /= stretch;
        p = bloomRot(c.x) * p;
        int ring = i - 6 * (i / 6);
        vec4 f = bloomFlower(p, vec4(b.z, b.w, b.x, c.x), vec4(a.w, c.z, c.w, float(ring)), hues);
        vec3 col = min(f.rgb, vec3(1.0)) * b.y;
        // Screen each flower over the ones before it: translucent layers that lighten, never clip.
        acc = acc + col * (1.0 - acc);
    }
    acc = min(acc * uBloomGain, vec3(0.8));
    fragColor = vec4(acc, 1.0);
}
