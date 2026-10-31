#version 300 es
precision highp float;

// ACID - the present pass.
//
// The feedback state is the picture; this pass turns it into a surface. The
// echo's brightness is read as a HEIGHT (lib_relief): the eye looks at it
// through a lean that rocks on the sound clocks, a light orbits it and rises
// with the melody, and each style shades it in one of two materials of its
// own (neon tubes, chrome, glass, membrane, ...) that swap on every hard cut.
// The tunnel the loop draws then reads as a lit, embossed, tilting relief
// instead of a flat screen of trails.
//
// The display-only dressing stays: the wraparound overdrive that folds hot
// values into neon contour bands, the CRT scanlines and aperture curvature
// for the styles that declare them, and the vignette and ambient floor.
// Nothing here feeds back, so it can be as nonlinear as it likes.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uState;
uniform vec2 uRes;
uniform float uScanline;  // 0..1 scanline mask strength
uniform float uCurve;     // 0..1 CRT barrel amount
uniform float uSat;       // saturation shape
uniform float uFloorHue;  // ambient floor tint, turns - silence is deep, not void
uniform float uOverdrive; // wraparound display gain: fract folds into neon contours
uniform float uHit;       // strike + kick envelope driving the fold, 0..1
uniform float uDepth;     // the relief's thickness in uv (0 = flat)
uniform int uLookBase;    // RELIEF_* look before the first hard cut and on every even one
uniform int uLookAlt;     // RELIEF_* look on every odd hard cut

//#include lib_sound

/**
 * The echo as a height: its luminance, lifted so the long dim tails still
 * have shape (a feedback image is mostly thin bright lines on black, and a
 * raw luminance would emboss only the lines). The cap keeps the hottest
 * crossings as plateaus rather than spikes.
 */
float reliefHeight(vec2 uv) {
    vec3 c = texture(uState, uv).rgb;
    float l = dot(c, vec3(0.32, 0.5, 0.18));
    return clamp(sqrt(l) * 1.1, 0.0, 1.0);
}

// The state grid is ~540 texels on its short side: tap normals two texels
// apart so the bilinear stair does not facet them.
#define RELIEF_EPS 0.003
#define RELIEF_BUMP 3.0
//#include lib_relief

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec2 uv = vUv;
    float aspect = uRes.x / uRes.y;
    if (uCurve > 0.0) {
        vec2 c = uv - 0.5;
        float r2 = dot(c * vec2(aspect, 1.0), c * vec2(aspect, 1.0));
        uv = 0.5 + c * (1.0 + uCurve * 0.22 * r2);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            fragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    // The relief swells with the kick and a surge, and lies flatter while the
    // music is absent, so the surface itself breathes with the song.
    float depth = uDepth * (0.6 + 0.4 * clamp(SND_PRESENCE, 0.0, 1.0) + 0.45 * clamp(SND_KICK_ENV, 0.0, 1.0) +
        0.35 * clamp(SND_HELD_SURGE, 0.0, 1.0));
    Relief rel = reliefMarch(uv, reliefTilt(), depth);
    vec3 color = texture(uState, rel.uv).rgb;

    // Wraparound overdrive, applied on DISPLAY only: gain past 1 folds back
    // through fract into neon contour bands - the acid signature - and
    // because nothing here feeds back, the fold can never compound into
    // static. It breathes on the transient envelope instead of free-running.
    if (uOverdrive > 0.0) {
        float alpha = 1.0 + uOverdrive * (0.4 + 1.6 * uHit);
        vec3 driven = fract(color * alpha);
        color = mix(color, driven, uOverdrive * (0.25 + 0.75 * uHit));
    }

    // Gentle S-shape: lifts mids of the echo tails without crushing black.
    color = color * color * (3.0 - 2.0 * color);
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, uSat);

    // The material. A whisper of the flat picture stays on top so the acid
    // colours never drown in a dark look (membrane, neon on black).
    int look = reliefLook(uLookBase, uLookAlt);
    vec3 shaded = reliefShade(look, color, rel, reliefLight(), clamp(SND_HAT_ENV + 0.3 * SND_TREBLE_PRESENCE, 0.0, 1.0));
    color = shaded + color * 0.1;

    if (uScanline > 0.0) {
        float line = 0.5 + 0.5 * cos(uv.y * uRes.y * 3.14159);
        color *= 1.0 - uScanline * 0.35 * line;
    }

    vec2 q = (uv - 0.5) * vec2(aspect, 1.0);
    float vig = 1.0 - 0.4 * dot(q, q);
    color *= vig;
    // A whisper of ambient floor so an empty loop is a deep field, not a
    // dead screen - the convention every family keeps for silence.
    color += hsv2rgb(vec3(fract(uFloorHue), 0.6, 1.0)) * 0.012 * vig;

    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
