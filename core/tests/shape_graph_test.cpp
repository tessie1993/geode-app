// Host checks for the shape graph (core/viz/ShapeGraph), the morphing CSG list lib_shapes melts into a style's
// own form by Shapes. ShapeGraph::sample and sampleInto are lib_shapes' maths line for line, so these hold the
// shader's rules too. Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml); no GL.
//
// Pinned: the same seed and the same frames give the same graph, float for float (an export replays the
// screen); Shapes at 0 or an empty graph leaves the style's distance and colour exactly as they were; a shape
// that has left leaves no trace (the graph equals one it was never in); every glide lands exactly on its
// target; primitive, operation, fold and presence changes complete in 0.9 s; nothing turns (the drift only
// translates, each group as one, and stops with the music's clock); the colour melts with the smooth minimum's
// own factor; the field is 1-Lipschitz on a sampled grid, mid-crossfade and partway folded included; no shape
// reaches past kReach; broken inputs stay finite. No sound and no tempo reach the graph: its inputs are params.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "viz/ShapeGraph.hpp"

using namespace geode::viz::shapes;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr float kFrame = 1.0f / 60.0f;
constexpr int kFloatsPerShape = kVec4PerShape * 4;

Inputs inputs(uint64_t seed, float spawn, float morph, float formula, float fractal, float shapes) {
    Inputs in;
    in.seed = seed;
    in.spawn = spawn;
    in.morph = morph;
    in.formula = formula;
    in.fractal = fractal;
    in.shapes = shapes;
    in.speed = 1.0f;
    return in;
}

// `frames` frames at 60 fps on the wall clock; the drift's clock runs on the same step when `clock`.
void run(ShapeGraph& g, const Inputs& in, int frames, bool clock) {
    for (int i = 0; i < frames; ++i) g.tick(in, kFrame, clock ? kFrame : 0.0f);
}

// A graph that has shown `in` from its first frame.
ShapeGraph primed(const Inputs& in) {
    ShapeGraph g;
    g.tick(in, kFrame, 0.0f);
    return g;
}

bool samePacked(const Packed& a, const Packed& b) {
    return a.shapes == b.shapes && a.count == b.count && a.mix == b.mix && a.fold == b.fold && a.melt == b.melt;
}

const float* shapeAt(const Packed& g, int i) { return g.shapes.data() + static_cast<size_t>(i * kFloatsPerShape); }

// Nothing crossfading: every primitive, operation and the fold's count has landed (packed as A == B, weight 0).
bool allLanded(const Packed& g) {
    for (int i = 0; i < kMaxShapes; ++i) {
        const float* v = shapeAt(g, i);
        if (v[10] != 0.0f || v[8] != v[9] || v[14] != 0.0f || v[12] != v[13]) return false;
    }
    return g.fold[2] == 0.0f && g.fold[0] == g.fold[1];
}

bool anyPrimRunning(const Packed& g) {
    for (int i = 0; i < kMaxShapes; ++i) {
        const float w = shapeAt(g, i)[10];
        if (w > 0.0f && w < 1.0f) return true;
    }
    return false;
}

bool anyPresencePartway(const Packed& g) {
    for (int i = 0; i < kMaxShapes; ++i) {
        const float p = shapeAt(g, i)[11];
        if (p > 0.0f && p < 1.0f) return true;
    }
    return false;
}

bool allWholeOrGone(const Packed& g) {
    for (int i = 0; i < kMaxShapes; ++i) {
        const float p = shapeAt(g, i)[11];
        if (p != 0.0f && p != 1.0f) return false;
    }
    return true;
}

bool finitePacked(const Packed& g) {
    for (const float v : g.shapes) {
        if (!std::isfinite(v)) return false;
    }
    for (const float v : g.fold) {
        if (!std::isfinite(v)) return false;
    }
    return std::isfinite(g.mix) && std::isfinite(g.melt);
}

// A style's own field for the melt: a ball of radius 0.7 at the origin (exact, so 1-Lipschitz).
float ball(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z) - 0.7f; }

// The same seed and the same frames give the same graph, float for float, so an export that ticks its own
// frames replays itself; another seed is another play, with other shapes.
void theSameSeedReplaysTheSameGraph() {
    ShapeGraph a;
    ShapeGraph b;
    ShapeGraph c;
    for (int f = 0; f < 600; ++f) {
        const float t = static_cast<float>(f) * kFrame;
        // Every input wanders, as a reaction route, a wave or a macro knob would move it.
        Inputs in = inputs(0x5EEDull, 0.5f + 0.5f * std::sin(0.7f * t), 0.5f + 0.5f * std::sin(1.3f * t),
                           0.5f + 0.5f * std::sin(0.29f * t), 0.5f + 0.5f * std::sin(0.53f * t), 0.6f + 0.3f * std::sin(0.41f * t));
        in.speed = 1.0f + 0.5f * std::sin(t);
        a.tick(in, kFrame, kFrame);
        b.tick(in, kFrame, kFrame);
        in.seed = 0xD1FFull;
        c.tick(in, kFrame, kFrame);
    }
    check(samePacked(a.packed(), b.packed()), "the same seed and frames give the same graph, float for float");
    check(!samePacked(a.packed(), c.packed()), "another seed gives another graph");
    a.reset();
    ShapeGraph fresh;
    const Inputs in = inputs(9, 0.7f, 0.3f, 0.8f, 0.4f, 0.5f);
    a.tick(in, kFrame, kFrame);
    fresh.tick(in, kFrame, kFrame);
    check(samePacked(a.packed(), fresh.packed()), "a reset graph is a fresh one: its first frame shows its target");
}

// Shapes at 0, or an empty graph, leaves the style's distance and colour exactly as they were.
void offLeavesTheStyleUntouched() {
    const ShapeGraph g = primed(inputs(11, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f));
    check(g.packed().mix == 0.0f && g.packed().count == kMaxShapes, "Shapes 0: every shape there, none melted in");
    Packed empty;
    empty.mix = 1.0f;
    bool same = true;
    for (float x = -2.0f; x <= 2.0f; x += 0.25f) {
        for (float y = -2.0f; y <= 2.0f; y += 0.5f) {
            for (float z = -2.0f; z <= 2.0f; z += 0.25f) {
                const float d = ball(x, y, z);
                const float colour = 0.1f * x + 0.3f;
                const Sample off = sampleInto(g.packed(), d, colour, x, y, z);
                const Sample none = sampleInto(empty, d, colour, x, y, z);
                const Sample alone = sample(empty, x, y, z);
                same = same && off.distance == d && off.colour == colour && none.distance == d && none.colour == colour &&
                       alone.distance == kFar && alone.colour == 0.0f;
            }
        }
    }
    check(same, "Shapes 0 or an empty graph: the style's own distance and colour, exactly");
}

// A shape that has shrunk away leaves no trace: the graph equals one it was never in, float for float.
void aShapeThatLeftLeavesNoTrace() {
    const uint64_t seed = 23;
    ShapeGraph left = primed(inputs(seed, 1.0f, 0.4f, 0.6f, 0.0f, 0.7f));
    check(left.packed().count == kMaxShapes, "Spawn 1: all eight shapes");
    const Inputs fewer = inputs(seed, 0.0f, 0.4f, 0.6f, 0.0f, 0.7f);
    run(left, fewer, 2 * 60, false);  // two seconds, past the 0.9 s shrink
    const ShapeGraph never = primed(fewer);
    check(samePacked(left.packed(), never.packed()), "the shapes that left are packed as the ones never there");
    bool same = true;
    for (float x = -2.0f; x <= 2.0f; x += 0.2f) {
        for (float y = -1.5f; y <= 1.5f; y += 0.3f) {
            for (float z = -2.0f; z <= 2.0f; z += 0.2f) {
                const Sample a = sample(left.packed(), x, y, z);
                const Sample b = sample(never.packed(), x, y, z);
                same = same && a.distance == b.distance && a.colour == b.colour;
            }
        }
    }
    check(same, "and the field is the same at every point");
}

// Every glide lands exactly on its target: a settled graph is the one a fresh renderer shows for the same params.
void glidesConverge() {
    const Inputs from = inputs(41, 0.3f, 0.1f, 0.2f, 0.1f, 0.3f);
    const Inputs to = inputs(41, 0.9f, 0.8f, 0.7f, 0.5f, 0.9f);
    ShapeGraph g = primed(from);
    // Far enough to tell a glide from a jump: the first frame moves only a little of the way.
    g.tick(to, kFrame, 0.0f);
    check(!samePacked(g.packed(), primed(to).packed()), "a changed param glides, it does not jump");
    run(g, to, 12 * 60, false);
    check(samePacked(g.packed(), primed(to).packed()), "every glide and crossfade lands exactly on its target");
    // A glide runs on wall time: a frame of no time moves nothing.
    ShapeGraph still = primed(from);
    const Packed before = still.packed();
    still.tick(to, 0.0f, 0.0f);
    bool held = true;
    for (int i = 0; i < kMaxShapes; ++i) {
        const float* a = shapeAt(before, i);
        const float* b = shapeAt(still.packed(), i);
        for (int k = 0; k < kFloatsPerShape; ++k) {
            if (k == 8 || k == 9 || k == 10 || k == 12 || k == 13 || k == 14) continue;  // a crossfade's new end is named at once
            held = held && a[k] == b[k];
        }
    }
    check(held && still.packed().fold[3] == before.fold[3] && still.packed().melt == before.melt, "a step of no time glides nothing");
}

// A primitive, operation, fold count or presence change completes in 0.9 s, linearly: 54 frames at 60 fps.
void crossfadesCompleteInNineTenths() {
    // Formula's soft mix (ball, capsule, ring) to its hard one (block, diamond, drum): every primitive changes.
    ShapeGraph g = primed(inputs(31, 1.0f, 0.5f, 0.05f, 0.0f, 0.6f));
    check(allLanded(g.packed()), "a fresh graph shows its target, nothing crossfading");
    const Inputs hard = inputs(31, 1.0f, 0.5f, 0.3f, 0.0f, 0.6f);
    run(g, hard, 52, false);
    check(anyPrimRunning(g.packed()), "two frames short of 0.9 s the primitives still crossfade");
    run(g, hard, 4, false);
    check(allLanded(g.packed()), "two frames past 0.9 s every crossfade has landed");
    bool hardOnes = true;
    for (int i = 0; i < kMaxShapes; ++i) {
        const int prim = static_cast<int>(shapeAt(g.packed(), i)[8]);
        hardOnes = hardOnes && (prim == static_cast<int>(Prim::Block) || prim == static_cast<int>(Prim::Diamond) ||
                                prim == static_cast<int>(Prim::Drum));
    }
    check(hardOnes, "on the hard mix's primitives");

    // Shapes growing in (Spawn 0 to 1) and the fold's count (Fractal 0.25: three folds, to 0.65: five).
    ShapeGraph grow = primed(inputs(37, 0.0f, 0.5f, 0.5f, 0.25f, 0.6f));
    check(grow.packed().fold[0] == 3.0f && grow.packed().fold[2] == 0.0f, "three folds, landed");
    const Inputs more = inputs(37, 1.0f, 0.5f, 0.5f, 0.65f, 0.6f);
    run(grow, more, 52, false);
    check(anyPresencePartway(grow.packed()), "two frames short of 0.9 s the new shapes are still growing in");
    check(grow.packed().fold[2] > 0.0f && grow.packed().fold[2] < 1.0f, "and the fold's count still crossfades");
    run(grow, more, 4, false);
    check(allWholeOrGone(grow.packed()) && grow.packed().count == kMaxShapes, "two frames past it every shape is whole");
    check(grow.packed().fold[0] == 5.0f && grow.packed().fold[1] == 5.0f && grow.packed().fold[2] == 0.0f, "on five folds");
}

// Nothing turns: between changes only the places move, each group as one, by at most the drift's reach, and
// the field moves as those translations do. Speed 0 or no music step holds it.
void theDriftOnlyTranslates() {
    const Inputs in = inputs(53, 1.0f, 0.5f, 0.8f, 0.0f, 0.7f);
    ShapeGraph g = primed(in);
    const Packed before = g.packed();
    run(g, in, 180, true);
    const Packed after = g.packed();
    bool shapeKept = true;
    bool groupAsOne = true;
    bool withinReach = true;
    bool moved = false;
    const int tie[kMaxShapes] = {0, 0, 0, 3, 3, 5, 5, 7};  // each shape's head (ShapeGraph's tree)
    for (int i = 0; i < kMaxShapes; ++i) {
        const float* a = shapeAt(before, i);
        const float* b = shapeAt(after, i);
        for (int k = 3; k < kFloatsPerShape; ++k) shapeKept = shapeKept && a[k] == b[k];
        const float* ha = shapeAt(before, tie[i]);
        const float* hb = shapeAt(after, tie[i]);
        for (int c = 0; c < 3; ++c) {
            groupAsOne = groupAsOne && std::fabs((b[c] - hb[c]) - (a[c] - ha[c])) < 1e-5f;
            withinReach = withinReach && std::fabs(b[c] - a[c]) <= 2.0f * kDriftReach + 1e-5f;
            moved = moved || b[c] != a[c];
        }
    }
    check(moved, "the music's clock drifts the graph");
    check(shapeKept, "only places move: size, colour, primitive, operation, presence and blend stay");
    check(groupAsOne, "a group drifts as one");
    check(withinReach, "by no more than the drift's reach");

    // The field depends on a place only through p - place: moving every place moves the field with it.
    Packed shifted = before;
    const float dx = 0.13f, dy = -0.07f, dz = 0.21f;
    for (int i = 0; i < kMaxShapes; ++i) {
        float* v = shifted.shapes.data() + static_cast<size_t>(i * kFloatsPerShape);
        v[0] += dx;
        v[1] += dy;
        v[2] += dz;
    }
    float worst = 0.0f;
    for (float x = -1.8f; x <= 1.8f; x += 0.3f) {
        for (float y = -1.2f; y <= 1.2f; y += 0.3f) {
            for (float z = -1.8f; z <= 1.8f; z += 0.3f) {
                worst = std::fmax(worst, std::fabs(sample(shifted, x + dx, y + dy, z + dz).distance - sample(before, x, y, z).distance));
            }
        }
    }
    check(worst < 1e-4f, "the field translates with its places, unturned");

    ShapeGraph quiet = primed(in);
    run(quiet, in, 180, false);
    check(samePacked(quiet.packed(), before), "no music step, no drift");
    Inputs stopped = in;
    stopped.speed = 0.0f;
    ShapeGraph held = primed(stopped);
    run(held, stopped, 180, true);
    check(samePacked(held.packed(), primed(stopped).packed()), "Speed 0 holds the drift");
}

// The colour melts with the smooth minimum's own factor: the share of the colour that stays the style's equals
// the share of the style in the distance's gradient (its derivative by the style's distance), at every blend.
void theColourBlendsWithTheDistance() {
    const ShapeGraph g = primed(inputs(61, 1.0f, 0.6f, 0.5f, 0.0f, 0.6f));
    const Packed& pk = g.packed();
    check(pk.mix > 0.0f && pk.melt > 0.0f, "Shapes on, a melt radius");
    const float eps = 1e-3f;
    const float ts[] = {-0.6f, -0.2f, 0.3f, 0.7f};
    int tried = 0;
    float worstGradient = 0.0f;
    float worstFactor = 0.0f;
    for (float x = -1.0f; x <= 1.0f; x += 0.5f) {
        for (float y = -1.0f; y <= 1.0f; y += 0.5f) {
            for (float z = -1.0f; z <= 1.0f; z += 0.5f) {
                const Sample s = sample(pk, x, y, z);
                if (!(s.distance < 0.5f * kFar)) continue;
                for (const float t : ts) {
                    // The style's distance set where the smooth minimum's weight on it is h, well inside the blend.
                    const float d = s.distance + t * pk.melt;
                    const float h = 0.5f - 0.5f * t;
                    const float colourShare = sampleInto(pk, d, 1.0f, x, y, z).colour - sampleInto(pk, d, 0.0f, x, y, z).colour;
                    const float slope =
                        (sampleInto(pk, d + eps, 0.0f, x, y, z).distance - sampleInto(pk, d - eps, 0.0f, x, y, z).distance) / (2.0f * eps);
                    worstGradient = std::fmax(worstGradient, std::fabs(colourShare - slope));
                    worstFactor = std::fmax(worstFactor, std::fabs(colourShare - (1.0f - pk.mix * (1.0f - h))));
                    ++tried;
                }
            }
        }
    }
    check(tried > 0, "points inside the melt");
    check(worstGradient < 2e-3f, "the colour's share is the distance's: the same blend factor");
    check(worstFactor < 1e-4f, "and it is the smooth minimum's weight, melted in by Shapes");
}

// The graph's field, and the graph melted into a 1-Lipschitz style, never change faster than the distance
// they are sampled over, so a march that was safe stays safe: every formula mix, folded none, partway and
// whole, and mid-crossfade (primitives, operations, presence and the fold's count all partway).
void theFieldIsOneLipschitz() {
    std::vector<Packed> graphs;
    for (const uint64_t seed : {3ull, 17ull, 29ull}) {
        for (const float formula : {0.1f, 0.35f, 0.6f, 0.9f}) {
            for (const float fractal : {0.0f, 0.2f, 0.9f}) graphs.push_back(primed(inputs(seed, 1.0f, 0.9f, formula, fractal, 1.0f)).packed());
        }
        // The soft mix to the hard one (every primitive changes), Spawn 0 to 1, three folds to five, partway.
        ShapeGraph moving = primed(inputs(seed, 0.0f, 0.2f, 0.05f, 0.25f, 0.5f));
        run(moving, inputs(seed, 1.0f, 0.9f, 0.3f, 0.65f, 1.0f), 25, true);
        check(anyPrimRunning(moving.packed()) && anyPresencePartway(moving.packed()) && moving.packed().fold[2] > 0.0f,
              "a graph caught mid-crossfade");
        graphs.push_back(moving.packed());
    }
    const float step = 0.15f;
    const float slack = step * 1e-4f + 1e-4f;
    float worst = 0.0f;
    float worstInto = 0.0f;
    for (const Packed& pk : graphs) {
        for (float x = -2.4f; x <= 2.4f; x += step) {
            for (float y = -2.4f; y <= 2.4f; y += step) {
                for (float z = -2.4f; z <= 2.4f; z += step) {
                    const float d0 = sample(pk, x, y, z).distance;
                    const float i0 = sampleInto(pk, ball(x, y, z), 0.0f, x, y, z).distance;
                    const float dn[3] = {sample(pk, x + step, y, z).distance, sample(pk, x, y + step, z).distance,
                                         sample(pk, x, y, z + step).distance};
                    const float in[3] = {sampleInto(pk, ball(x + step, y, z), 0.0f, x + step, y, z).distance,
                                         sampleInto(pk, ball(x, y + step, z), 0.0f, x, y + step, z).distance,
                                         sampleInto(pk, ball(x, y, z + step), 0.0f, x, y, z + step).distance};
                    for (int k = 0; k < 3; ++k) {
                        worst = std::fmax(worst, std::fabs(dn[k] - d0) - step);
                        worstInto = std::fmax(worstInto, std::fabs(in[k] - i0) - step);
                    }
                }
            }
        }
    }
    check(worst <= slack, "the graph's distance is 1-Lipschitz on the grid");
    check(worstInto <= slack, "and so is the graph melted into a style");
}

// No present shape reaches past kReach (lib_shapes' SHAPES_REACH), at full spread, Morph and Spawn, every
// formula mix, folded or not: a style's bounding sphere of that radius holds the whole graph.
void nothingReachesPastTheBound() {
    bool inside = true;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        for (const float formula : {0.1f, 0.35f, 0.6f, 0.9f}) {
            for (const float fractal : {0.0f, 0.2f, 1.0f}) {
                const ShapeGraph g = primed(inputs(seed, 1.0f, 1.0f, formula, fractal, 1.0f));
                for (int k = 0; k < 64; ++k) {
                    // Directions spread over the sphere (a golden-angle spiral).
                    const float u = 1.0f - 2.0f * (static_cast<float>(k) + 0.5f) / 64.0f;
                    const float r = std::sqrt(std::fmax(0.0f, 1.0f - u * u));
                    const float a = 2.3999632f * static_cast<float>(k);
                    const float x = kReach * r * std::cos(a);
                    const float y = kReach * u;
                    const float z = kReach * r * std::sin(a);
                    inside = inside && sample(g.packed(), x, y, z).distance > 0.0f &&
                             sampleInto(g.packed(), ball(x, y, z), 0.0f, x, y, z).distance > 0.0f;
                }
            }
        }
    }
    check(inside, "nothing reaches past kReach, the graph alone or melted into a style");
}

// Broken inputs and steps stay finite: a NaN reads as 0, an infinite step as none.
void brokenInputsStayFinite() {
    const float nan = std::nanf("");
    const float inf = INFINITY;
    Inputs in = inputs(5, nan, inf, -inf, nan, nan);
    in.speed = nan;
    ShapeGraph g;
    g.tick(in, inf, nan);
    g.tick(in, -1.0f, inf);
    g.tick(inputs(5, 2.0f, -1.0f, 7.0f, 3.0f, -2.0f), kFrame, kFrame);
    check(finitePacked(g.packed()), "NaN, infinite and out-of-range inputs leave a finite graph");
    const Sample s = sampleInto(g.packed(), 0.3f, 0.2f, 0.1f, 0.2f, 0.3f);
    check(std::isfinite(s.distance) && std::isfinite(s.colour), "and a finite field");
}

}  // namespace

int main() {
    theSameSeedReplaysTheSameGraph();
    offLeavesTheStyleUntouched();
    aShapeThatLeftLeavesNoTrace();
    glidesConverge();
    crossfadesCompleteInNineTenths();
    theDriftOnlyTranslates();
    theColourBlendsWithTheDistance();
    theFieldIsOneLipschitz();
    nothingReachesPastTheBound();
    brokenInputsStayFinite();
    if (failures == 0) std::printf("shape_graph_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
