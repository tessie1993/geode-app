#version 300 es
precision highp float;

// The note blooms (core/viz/NoteBloomPass): one bloom per sounding note in the style's look (lib_bloom),
// screened over the finished frame of every style, after the composite, the last draw of the frame.
//
// NoteResponse decides everything per bloom on the CPU: where it is (with its travel and parallax), how
// big, its hue, how open, how bright, its petals and depth, its turn and its tilt in 3D. This pass only
// draws. It is scissored to the blooms' bounding box, and every pixel tests each bloom's disc before
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
// d = (tilt x, tilt y, depth 0 near .. 1 far, unused).
uniform vec4 uBloomD[8];
// 1 flower, 2 glow, 3 ripple, 4 burst; 0 (never uploaded) draws the flower.
uniform int uBloomLook;
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
        vec4 tilt = uBloomD[i];
        float radius = a.z * shortSide;
        float stretch = c.y;
        vec2 d = frag - a.xy * uResolution;
        // Early out: outside this bloom's disc (stretched up for high notes) there is nothing to draw.
        float reach = 1.25 * radius * max(stretch, 1.0);
        if (dot(d, d) > reach * reach) continue;
        vec2 p = d / radius;
        p.y /= stretch;
        p = bloomUntilt(p, tilt.xy);
        p = bloomRot(c.x) * p;
        int ring = i - 6 * (i / 6);
        vec4 f = bloomDraw(uBloomLook, p, vec4(b.z, b.w, b.x, c.x), vec4(a.w, c.z, c.w, float(ring)), hues);
        // A far bloom is softer as well as dimmer: its colour leans toward its own mean, a cheap depth haze.
        vec3 col = min(f.rgb, vec3(1.0));
        col = mix(col, vec3(dot(col, vec3(0.333))), 0.35 * tilt.z) * b.y;
        // Screen each flower over the ones before it: translucent layers that lighten, never clip.
        acc = acc + col * (1.0 - acc);
    }
    acc = min(acc * uBloomGain, vec3(0.8));
    fragColor = vec4(acc, 1.0);
}
