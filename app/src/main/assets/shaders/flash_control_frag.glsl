#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 3 of 4 (core/viz/FlashLimitPass): flash_limit::step, line for line (host
// reference and proof: core/viz/FlashLimiter.cpp, core/tests/flash_limit_test.cpp). The decision
// couples every tile (regions overlap, and holding one region can tip another), so every fragment of
// the 16 x 16 control texture runs the whole step on the same inputs and writes only its own entry:
// texels 0..143 are tiles, 144..152 the regions' luminance detectors, 153..161 their red detectors.
// 162 fragments of a few thousand operations each; the rest return at once.
//   tile:     ctrl0 = shown (rg), releasing (b), guard (a); ctrl1 = gain (rg), lift (ba)
//   detector: ctrl0 = extreme (rg), up (b), pending (a); ctrl1 = ages 0, 1; ctrl2 = ages 2, 3;
//             ctrl3 = age 4 (rg), primed (b; read from texel 144 only)
uniform sampler2D uTileLumRed;
uniform sampler2D uTileGuarded;
uniform sampler2D uCtrl0;
uniform sampler2D uCtrl1;
uniform sampler2D uCtrl2;
uniform sampler2D uCtrl3;
uniform ivec2 uGrid;
uniform ivec2 uFrameSize;
uniform ivec2 uCellCount;
uniform float uDt;
uniform float uFlSwing;
uniform float uFlDarkLimit;
uniform float uFlWindow;
uniform float uRedSwing;
uniform float uFlDetectSlack;
uniform float uFlWindowSlack;
uniform float uFlReleaseRate;
uniform float uFlMaxDt;

layout(location = 0) out vec4 outCtrl0;
layout(location = 1) out vec4 outCtrl1;
layout(location = 2) out vec4 outCtrl2;
layout(location = 3) out vec4 outCtrl3;

const int kTiles = 144;
const int kRegions = 9;
const int kTracks = 18;  // 0..8 luminance, 9..17 red
const int kSlots = 5;
const int kMaxIterations = 2 * kRegions + 2;
const float kEmptyAge = 2.0;
const int kCellsPerTile = 8;

float lumIn[kTiles];
float redIn[kTiles];
float guardedIn[kTiles];
float area[kTiles];
float prevShown[kTiles];
bool prevReleasing[kTiles];
bool held[kTiles];
bool guarded[kTiles];
float shown[kTiles];
float redShown[kTiles];
float gainOf[kTiles];
float liftOf[kTiles];
bool tUp[kTracks];
bool tPending[kTracks];
float tExtreme[kTracks];
float tAges[kTracks * kSlots];
float regionValue[kTracks];  // 0..8 luminance, 9..17 red

ivec2 ctrlTexel(int i) { return ivec2(i % 16, i / 16); }

float quantiseAge(float age) { return floor(clamp(age / kEmptyAge, 0.0, 1.0) * 65535.0) / 65535.0 * kEmptyAge; }

int cellEnd(int c, int size, int cells) { return min(((c + 1) * size) / cells, size); }

void axisWindow(int n, int k, out int start, out int size) {
    size = (n + 1) / 2;
    start = k == 0 ? 0 : k == 1 ? (n - size) / 2 : n - size;
}

bool inRegion(int t, int r) {
    int x0, w, y0, h;
    axisWindow(uGrid.x, r % 3, x0, w);
    axisWindow(uGrid.y, r / 3, y0, h);
    int x = t % uGrid.x;
    int y = t / uGrid.x;
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

void regionMeans() {
    for (int r = 0; r < kRegions; r++) {
        float a = 0.0;
        float sumLum = 0.0;
        float sumRed = 0.0;
        for (int t = 0; t < kTiles; t++) {
            if (!inRegion(t, r)) continue;
            a += area[t];
            sumLum += area[t] * shown[t];
            sumRed += area[t] * redShown[t];
        }
        regionValue[r] = sumLum / a;
        regionValue[kRegions + r] = sumRed / a;
    }
}

int eventsInWindow(int k) {
    int n = 0;
    for (int s = 0; s < kSlots; s++) n += tAges[k * kSlots + s] <= uFlWindow + uFlWindowSlack ? 1 : 0;
    return n;
}

// (flip, counts), as probe() in FlashLimiter.cpp.
bvec2 probe(int k, float v) {
    bool lum = k < kRegions;
    float theta = (lum ? uFlSwing : uRedSwing) - uFlDetectSlack;
    float dark = uFlDarkLimit + uFlDetectSlack;
    float x = tExtreme[k];
    if (tUp[k]) {
        if (v < x && x - v >= theta) return bvec2(true, !lum || v < dark);
        return bvec2(false);
    }
    if (v <= x) return bvec2(false, lum && tPending[k] && v < dark);
    if (v - x >= theta) return bvec2(true, !lum || x < dark);
    return bvec2(false);
}

bool allowed(int k, bvec2 p) {
    if (!p.y) return true;
    int events = eventsInWindow(k);
    if (k < kRegions) return events + 1 <= kSlots;
    return !p.x || tUp[k] || events + 2 <= kSlots;
}

void commit(int k, float v) {
    bool lum = k < kRegions;
    bvec2 p = probe(k, v);
    if (p.x) {
        tUp[k] = !tUp[k];
        tExtreme[k] = flQuantise(v);
        tPending[k] = lum && !tUp[k] && !p.y;
    } else if (tUp[k] ? v > tExtreme[k] : v < tExtreme[k]) {
        tExtreme[k] = flQuantise(v);
        if (p.y) tPending[k] = false;
    }
    if (p.y) {
        int oldest = 0;
        for (int s = 1; s < kSlots; s++) {
            if (tAges[k * kSlots + s] > tAges[k * kSlots + oldest]) oldest = s;
        }
        tAges[k * kSlots + oldest] = 0.0;
    }
}

void evaluate(float release) {
    for (int t = 0; t < kTiles; t++) {
        float base = 0.0;
        float target = 0.0;
        for (int pass = 0; pass < 2; pass++) {
            base = guarded[t] ? guardedIn[t] : lumIn[t];
            target = held[t] ? prevShown[t] : prevReleasing[t] ? prevShown[t] + clamp(base - prevShown[t], -release, release) : base;
            if (target != base && !guarded[t] && redIn[t] > 0.0) {
                guarded[t] = true;
                continue;
            }
            break;
        }
        shown[t] = flQuantise(target);
        gainOf[t] = shown[t] < base ? shown[t] / base : 1.0;
        liftOf[t] = shown[t] > base ? (shown[t] - base) / (1.0 - base) : 0.0;
        redShown[t] = guarded[t] ? 0.0 : redIn[t];
    }
    regionMeans();
}

void main() {
    int i = int(gl_FragCoord.y) * 16 + int(gl_FragCoord.x);
    outCtrl0 = vec4(0.0);
    outCtrl1 = vec4(0.0);
    outCtrl2 = vec4(0.0);
    outCtrl3 = vec4(0.0);
    if (i >= kTiles + kTracks) return;

    for (int t = 0; t < kTiles; t++) {
        ivec2 tile = ivec2(t % uGrid.x, t / uGrid.x);
        vec4 a = texelFetch(uTileLumRed, tile, 0);
        lumIn[t] = flUnpack16(a.rg);
        redIn[t] = flUnpack16(a.ba);
        guardedIn[t] = flUnpack16(texelFetch(uTileGuarded, tile, 0).rg);
        ivec2 c0 = tile * kCellsPerTile;
        int w = cellEnd(c0.x + kCellsPerTile - 1, uFrameSize.x, uCellCount.x) - flCellStart(c0.x, uFrameSize.x, uCellCount.x);
        int h = cellEnd(c0.y + kCellsPerTile - 1, uFrameSize.y, uCellCount.y) - flCellStart(c0.y, uFrameSize.y, uCellCount.y);
        area[t] = float(max(w, 1) * max(h, 1));
        vec4 s = texelFetch(uCtrl0, ctrlTexel(t), 0);
        prevShown[t] = flUnpack16(s.rg);
        prevReleasing[t] = s.b > 0.5;
        held[t] = false;
        guarded[t] = false;
        gainOf[t] = 1.0;
        liftOf[t] = 0.0;
    }
    bool primed = texelFetch(uCtrl3, ctrlTexel(kTiles), 0).b > 0.5;

    if (!primed) {
        // The first frame after create, a surface change or a turn is shown as drawn.
        for (int t = 0; t < kTiles; t++) {
            shown[t] = lumIn[t];
            redShown[t] = redIn[t];
            prevReleasing[t] = false;
        }
        regionMeans();
        for (int k = 0; k < kTracks; k++) {
            tUp[k] = false;
            tPending[k] = false;
            tExtreme[k] = flQuantise(regionValue[k]);
            for (int s = 0; s < kSlots; s++) tAges[k * kSlots + s] = kEmptyAge;
        }
    } else {
        float stepDt = clamp(uDt, 0.0, uFlMaxDt);
        for (int k = 0; k < kTracks; k++) {
            ivec2 texel = ctrlTexel(kTiles + k);
            vec4 c0 = texelFetch(uCtrl0, texel, 0);
            vec4 c1 = texelFetch(uCtrl1, texel, 0);
            vec4 c2 = texelFetch(uCtrl2, texel, 0);
            vec4 c3 = texelFetch(uCtrl3, texel, 0);
            tExtreme[k] = flUnpack16(c0.rg);
            tUp[k] = c0.b > 0.5;
            tPending[k] = c0.a > 0.5;
            float ages[kSlots] = float[kSlots](flUnpack16(c1.rg), flUnpack16(c1.ba), flUnpack16(c2.rg), flUnpack16(c2.ba), flUnpack16(c3.rg));
            for (int s = 0; s < kSlots; s++) tAges[k * kSlots + s] = quantiseAge(min(ages[s] * kEmptyAge + stepDt, kEmptyAge));
        }
        float release = uFlReleaseRate * stepDt;
        bool settled = false;
        for (int iteration = 0; iteration < kMaxIterations; iteration++) {
            if (settled) break;
            evaluate(release);
            settled = true;
            for (int r = 0; r < kRegions; r++) {
                bool holdLum = !allowed(r, probe(r, regionValue[r]));
                bool guardRed = !allowed(kRegions + r, probe(kRegions + r, regionValue[kRegions + r]));
                if (!holdLum && !guardRed) continue;
                for (int t = 0; t < kTiles; t++) {
                    if (!inRegion(t, r)) continue;
                    if (holdLum && !held[t]) {
                        held[t] = true;
                        settled = false;
                    }
                    if (guardRed && !guarded[t]) {
                        guarded[t] = true;
                        settled = false;
                    }
                }
            }
        }
        if (!settled) {
            for (int t = 0; t < kTiles; t++) {
                held[t] = true;
                guarded[t] = true;
            }
            evaluate(release);
        }
        for (int k = 0; k < kTracks; k++) commit(k, regionValue[k]);
    }

    if (i < kTiles) {
        float base = guarded[i] ? guardedIn[i] : lumIn[i];
        bool releasing = held[i] || (prevReleasing[i] && shown[i] != base);
        outCtrl0 = vec4(flPack16(shown[i]), releasing ? 1.0 : 0.0, guarded[i] ? 1.0 : 0.0);
        outCtrl1 = vec4(flPack16(gainOf[i]), flPack16(liftOf[i]));
        return;
    }
    int k = i - kTiles;
    int o = k * kSlots;
    // Ages are already rounded down to a 16-bit step (quantiseAge); stored as they are.
    outCtrl0 = vec4(flPack16(tExtreme[k]), tUp[k] ? 1.0 : 0.0, tPending[k] ? 1.0 : 0.0);
    outCtrl1 = vec4(flPack16(tAges[o] / kEmptyAge), flPack16(tAges[o + 1] / kEmptyAge));
    outCtrl2 = vec4(flPack16(tAges[o + 2] / kEmptyAge), flPack16(tAges[o + 3] / kEmptyAge));
    outCtrl3 = vec4(flPack16(tAges[o + 4] / kEmptyAge), 1.0, 0.0);
}
