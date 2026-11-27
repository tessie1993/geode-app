// The per-frame UV transform for the feedback pass: uvFor() and fold() of
// core/viz/liquid/FeedbackMotion.hpp, term for term. Change both
// together. The C++ side sanitizes the values before LiquidPass uploads them, so nothing here checks
// for non-finite input.
//
// Every term that depends only on the frame's motion and the clock (the rotation's cos and sin, the
// warp's four drifting frequencies and phases, 1 / warp scale, which stages run) is FrameTerms, derived
// once a frame on the CPU (motion::derive); a pixel only applies them.

// FrameTerms::flags. A stage left out is exact: it would add 0 or multiply by 1.
const int kFeedbackMoves = 1;       // anything but the identity
const int kFeedbackRadialZoom = 2;  // zoomExp is not 1
const int kFeedbackWarps = 4;       // warp is not 0

uniform int uMotionFlags;
uniform float uZoom;
uniform float uZoomExp;
uniform float uCx;
uniform float uCy;
uniform float uDx;
uniform float uDy;
uniform float uSx;
uniform float uSy;
uniform vec2 uRotCS;          // cos, sin of the rotation
uniform float uWarp;
uniform float uWarpInvScale;  // 1 / warp scale
uniform vec4 uWarpFreq;       // the four drifting frequencies, f0..f3
uniform vec4 uWarpPhase;      // each warp term's phase

bool feedbackMoves() { return (uMotionFlags & kFeedbackMoves) != 0; }

// The UV of the last frame a destination pixel samples; uv is GL's (v up), aspect width / height.
vec2 uvFor(vec2 uv, float aspect) {
    if (!feedbackMoves()) return uv;
    float aspX = aspect < 1.0 ? aspect : 1.0;
    float aspY = aspect > 1.0 ? 1.0 / aspect : 1.0;
    float x = uv.x * 2.0 - 1.0;
    float y = uv.y * 2.0 - 1.0;

    float zoom2 = uZoom;
    if ((uMotionFlags & kFeedbackRadialZoom) != 0) {
        float rad = sqrt(x * x * aspX * aspX + y * y * aspY * aspY);
        zoom2 = pow(uZoom, pow(uZoomExp, rad * 2.0 - 1.0));
    }
    float zoomInv = 1.0 / zoom2;
    float u = x * aspX * 0.5 * zoomInv + 0.5;
    float v = -y * aspY * 0.5 * zoomInv + 0.5;

    u = (u - uCx) / uSx + uCx;
    v = (v - uCy) / uSy + uCy;

    if ((uMotionFlags & kFeedbackWarps) != 0) {
        float si = uWarpInvScale;
        vec4 fq = uWarpFreq;
        u += uWarp * sin(uWarpPhase.x + si * (x * fq.x - y * fq.w));
        v += uWarp * cos(uWarpPhase.y - si * (x * fq.z + y * fq.y));
        u += uWarp * cos(uWarpPhase.z - si * (x * fq.y - y * fq.z));
        v += uWarp * sin(uWarpPhase.w + si * (x * fq.x + y * fq.w));
    }

    float u2 = u - uCx;
    float v2 = v - uCy;
    u = u2 * uRotCS.x - v2 * uRotCS.y + uCx;
    v = u2 * uRotCS.y + v2 * uRotCS.x + uCy;

    u -= uDx;
    v -= uDy;

    u = (u - 0.5) / aspX + 0.5;
    v = (v - 0.5) / aspY + 0.5;
    return vec2(u, 1.0 - v);
}

// Mirror fold into 0..1: an edge pulled inward reflects the picture, never smears it into stripes.
float fold(float p) { return 1.0 - abs(1.0 - mod(p, 2.0)); }
vec2 fold(vec2 p) { return vec2(fold(p.x), fold(p.y)); }
