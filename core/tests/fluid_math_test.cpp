// Host-side checks for the fluid engine's pure maths (core/viz/fluid/FluidMath, FluidQuality).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system
// compiler; no GL, no device.
//
// The grid passes themselves run on the GPU, so this file carries CPU references of them, each
// written to mirror its shader line for line: the multigrid V-cycle (fluid_mg_*_frag), damped
// Jacobi (the solver it replaced), semi-Lagrangian advection (fluid_advect_frag) and the MacCormack
// corrector (fluid_maccormack_frag). They live here and not in the library because nothing on the
// device calls them; what the device does call (the solve plan, the corrector's clamp, the splat
// falloff, the heat and particle maths) is tested against them.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "viz/fluid/FluidMath.hpp"
#include "viz/fluid/FluidQuality.hpp"

using namespace geode::viz::fluid;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// ---- A cell-centred grid with clamped (texelFetch-and-clamp) reads, as every pass samples. ------

struct Grid {
    int w = 0;
    int h = 0;
    std::vector<float> v;
    Grid(int w_, int h_) : w(w_), h(h_), v(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0.0f) {}
    float at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    }
    float& ref(int x, int y) { return v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
};

double rms(const Grid& g) {
    double s = 0.0;
    for (float f : g.v) s += static_cast<double>(f) * f;
    return std::sqrt(s / static_cast<double>(g.v.size()));
}

// lap p = rhs, the system fluid_mg_smooth_frag relaxes (the old Jacobi pass's alpha = -h^2 form).
Grid residual(const Grid& p, const Grid& rhs, float h2) {
    Grid r(p.w, p.h);
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            const float lap = (p.at(x - 1, y) + p.at(x + 1, y) + p.at(x, y - 1) + p.at(x, y + 1) - 4.0f * p.at(x, y)) / h2;
            r.ref(x, y) = rhs.at(x, y) - lap;
        }
    }
    return r;
}

// fluid_mg_smooth_frag: one damped-Jacobi pass.
void smooth(Grid& p, const Grid& rhs, float h2, float omega) {
    Grid q = p;
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            const float c = p.at(x, y);
            const float j = (p.at(x - 1, y) + p.at(x + 1, y) + p.at(x, y - 1) + p.at(x, y + 1) - h2 * rhs.at(x, y)) * 0.25f;
            q.ref(x, y) = c + omega * (j - c);
        }
    }
    p = std::move(q);
}

// fluid_mg_restrict_frag: the 2x2 block's residuals, averaged, at the coarse cell.
Grid restrictResidual(const Grid& p, const Grid& rhs, float h2, int cw, int ch) {
    Grid c(cw, ch);
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            float s = 0.0f;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const int fx = std::min(2 * x + dx, p.w - 1);
                    const int fy = std::min(2 * y + dy, p.h - 1);
                    const float lap = (p.at(fx - 1, fy) + p.at(fx + 1, fy) + p.at(fx, fy - 1) + p.at(fx, fy + 1) - 4.0f * p.at(fx, fy)) / h2;
                    s += rhs.at(fx, fy) - lap;
                }
            }
            c.ref(x, y) = 0.25f * s;
        }
    }
    return c;
}

// fluid_mg_prolong_frag: p += the coarse correction, bilinear at the fine cell's centre.
void prolongAdd(Grid& p, const Grid& e) {
    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            const float cx = (static_cast<float>(x) + 0.5f) * 0.5f - 0.5f;
            const float cy = (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f;
            const int ix = static_cast<int>(std::floor(cx));
            const int iy = static_cast<int>(std::floor(cy));
            const float fx = cx - static_cast<float>(ix);
            const float fy = cy - static_cast<float>(iy);
            const float a = e.at(ix, iy) + (e.at(ix + 1, iy) - e.at(ix, iy)) * fx;
            const float b = e.at(ix, iy + 1) + (e.at(ix + 1, iy + 1) - e.at(ix, iy + 1)) * fx;
            p.ref(x, y) += a + (b - a) * fy;
        }
    }
}

// FluidSim::vCycle, recursively.
void vCycle(std::vector<Grid>& p, std::vector<Grid>& rhs, const std::vector<std::pair<int, int>>& levels, const std::vector<float>& h2,
            const math::SolvePlan& plan, int l) {
    const int last = plan.levels - 1;
    if (l == last) {
        std::fill(p[static_cast<size_t>(l)].v.begin(), p[static_cast<size_t>(l)].v.end(), 0.0f);
        for (int i = 0; i < plan.coarseIterations; ++i) smooth(p[static_cast<size_t>(l)], rhs[static_cast<size_t>(l)], h2[static_cast<size_t>(l)], math::kSmootherOmega);
        return;
    }
    const auto L = static_cast<size_t>(l);
    for (int i = 0; i < plan.smooth; ++i) smooth(p[L], rhs[L], h2[L], math::kSmootherOmega);
    rhs[L + 1] = restrictResidual(p[L], rhs[L], h2[L], levels[L + 1].first, levels[L + 1].second);
    std::fill(p[L + 1].v.begin(), p[L + 1].v.end(), 0.0f);
    vCycle(p, rhs, levels, h2, plan, l + 1);
    prolongAdd(p[L], p[L + 1]);
    for (int i = 0; i < plan.smooth; ++i) smooth(p[L], rhs[L], h2[L], math::kSmootherOmega);
}

Grid solveMultigrid(const Grid& div, float h0, int budget) {
    const auto levels = math::multigridLevels(div.w, div.h);
    const math::SolvePlan plan = math::planPressureSolve(budget, div.w, div.h);
    std::vector<Grid> p;
    std::vector<Grid> rhs;
    std::vector<float> h2;
    for (size_t l = 0; l < levels.size(); ++l) {
        p.emplace_back(levels[l].first, levels[l].second);
        rhs.emplace_back(levels[l].first, levels[l].second);
        const float hl = h0 * static_cast<float>(1 << l);
        h2.push_back(hl * hl);
    }
    rhs[0] = div;
    if (plan.levels >= 2) {
        for (int c = 0; c < plan.cycles; ++c) vCycle(p, rhs, levels, h2, plan, 0);
    }
    for (int i = 0; i < plan.extraPasses; ++i) smooth(p[0], rhs[0], h2[0], math::kSmootherOmega);
    return p[0];
}

// The solver this replaced: undamped Jacobi, `iterations` passes from zero.
Grid solveJacobi(const Grid& div, float h0, int iterations) {
    Grid p(div.w, div.h);
    for (int i = 0; i < iterations; ++i) smooth(p, div, h0 * h0, 1.0f);
    return p;
}

Grid blobbyDivergence(int w, int h, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    Grid d(w, h);
    for (int b = 0; b < 30; ++b) {
        const float cx = u(rng) * static_cast<float>(w);
        const float cy = u(rng) * static_cast<float>(h);
        const float s = u(rng) * 2.0f - 1.0f;
        const float r = static_cast<float>(h) * (0.01f + 0.10f * u(rng));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                d.ref(x, y) += s * std::exp(-(dx * dx + dy * dy) / (r * r));
            }
        }
    }
    double mean = 0.0;
    for (float f : d.v) mean += f;
    mean /= static_cast<double>(d.v.size());
    for (float& f : d.v) f -= static_cast<float>(mean);
    return d;
}

void multigridLevelsCoarsenCellCentred() {
    const auto l = math::multigridLevels(288, 640);
    check(l.size() == 6, "a 288-line grid coarsens to its sixth level");
    check(l[1] == std::make_pair(144, 320) && l[2] == std::make_pair(72, 160) && l[3] == std::make_pair(36, 80), "levels halve");
    const auto odd = math::multigridLevels(97, 171);
    check(odd[1] == std::make_pair(49, 86), "odd sides round up, (n + 1) / 2");
    check(math::multigridLevels(12, 40).size() == 1, "a grid too small to coarsen is one level");
    for (const auto& [w, h] : math::multigridLevels(64, 142)) check(std::min(w, h) >= math::kMinCoarseSide, "no level under the minimum side");
}

void solvePlanSpendsTheBudget() {
    for (const auto& [w, h] : {std::make_pair(64, 142), std::make_pair(128, 284), std::make_pair(288, 640), std::make_pair(24, 24)}) {
        float lastCost = 0.0f;
        for (int budget = 8; budget <= 40; ++budget) {
            const auto plan = math::planPressureSolve(budget, w, h);
            check(plan.cost <= static_cast<float>(budget) + 0.01f || plan.cycles == 1, "the plan stays inside its budget (past the one cycle it always runs)");
            check(plan.cost >= lastCost - 0.01f, "a bigger budget never buys less work");
            check(plan.draws <= 130, "the pass count stays bounded");
            check(plan.levels == 1 || (plan.cycles >= 1 && plan.cycles <= math::kMaxCycles), "cycles are in range");
            lastCost = plan.cost;
        }
    }
}

void multigridBeatsJacobiAtEveryBudget() {
    for (const auto& [w, h] : {std::make_pair(114, 64), std::make_pair(228, 128), std::make_pair(512, 288)}) {
        const Grid div = blobbyDivergence(w, h, static_cast<unsigned>(w));
        const float h0 = 2.0f / static_cast<float>(h);
        const double r0 = rms(residual(Grid(w, h), div, h0 * h0));
        for (int budget : {8, 16, 28, 40}) {
            const double mg = rms(residual(solveMultigrid(div, h0, budget), div, h0 * h0)) / r0;
            const double jac = rms(residual(solveJacobi(div, h0, budget), div, h0 * h0)) / r0;
            char what[160];
            std::snprintf(what, sizeof what, "multigrid (%.3f) leaves under half Jacobi's (%.3f) divergence at %dx%d, budget %d", mg, jac, w, h, budget);
            check(mg < 0.5 * jac, what);
        }
        const double low = rms(residual(solveMultigrid(div, h0, 8), div, h0 * h0));
        const double high = rms(residual(solveMultigrid(div, h0, 40), div, h0 * h0));
        check(high < low, "more solver iterations is still more accurate");
    }
}

// ---- Advection ------------------------------------------------------------------------------------

// fluid_advect_frag's trace in cell units with a uniform velocity; out-of-range reads clamp.
float bilerp(const Grid& g, float x, float y, float* lo = nullptr, float* hi = nullptr) {
    const float sx = x - 0.5f;
    const float sy = y - 0.5f;
    const int ix = static_cast<int>(std::floor(sx));
    const int iy = static_cast<int>(std::floor(sy));
    const float fx = sx - static_cast<float>(ix);
    const float fy = sy - static_cast<float>(iy);
    const float a = g.at(ix, iy);
    const float b = g.at(ix + 1, iy);
    const float c = g.at(ix, iy + 1);
    const float d = g.at(ix + 1, iy + 1);
    if (lo) *lo = std::min({a, b, c, d});
    if (hi) *hi = std::max({a, b, c, d});
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

Grid advectSl(const Grid& src, float ux, float uy) {
    Grid out(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) out.ref(x, y) = bilerp(src, static_cast<float>(x) + 0.5f - ux, static_cast<float>(y) + 0.5f - uy);
    }
    return out;
}

// fluid_maccormack_frag: forward from the source, backward from the forward result, clamped.
Grid advectMacCormack(const Grid& src, float ux, float uy) {
    const Grid fwd = advectSl(src, ux, uy);
    Grid out(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            const float cx = static_cast<float>(x) + 0.5f;
            const float cy = static_cast<float>(y) + 0.5f;
            float lo = 0.0f;
            float hi = 0.0f;
            const float f = bilerp(src, cx - ux, cy - uy, &lo, &hi);
            const float back = bilerp(fwd, cx + ux, cy + uy);
            out.ref(x, y) = math::macCormack(f, src.at(x, y), back, lo, hi);
        }
    }
    return out;
}

void macCormackCorrectorClamps() {
    check(math::macCormack(0.5f, 0.7f, 0.3f, 0.0f, 1.0f) == 0.7f, "the corrector adds half the round-trip error");
    check(math::macCormack(0.9f, 1.0f, 0.2f, 0.0f, 1.0f) == 1.0f, "and is clamped to the taps' maximum");
    check(math::macCormack(0.1f, 0.0f, 0.8f, 0.05f, 1.0f) == 0.05f, "and to their minimum");
    check(math::macCormack(0.4f, 0.4f, 0.4f, 0.0f, 1.0f) == 0.4f, "an exact trace is left alone");
}

void macCormackKeepsAPulseSharpWithoutOvershoot() {
    Grid pulse(96, 96);
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            const float dx = static_cast<float>(x) - 30.0f;
            const float dy = static_cast<float>(y) - 30.0f;
            pulse.ref(x, y) = std::exp(-(dx * dx + dy * dy) / 18.0f);
        }
    }
    Grid sl = pulse;
    Grid mc = pulse;
    for (int i = 0; i < 60; ++i) {
        sl = advectSl(sl, 0.37f, 0.23f);
        mc = advectMacCormack(mc, 0.37f, 0.23f);
    }
    float slPeak = 0.0f;
    float mcPeak = 0.0f;
    float mcMin = 1.0f;
    for (size_t i = 0; i < mc.v.size(); ++i) {
        slPeak = std::max(slPeak, sl.v[i]);
        mcPeak = std::max(mcPeak, mc.v[i]);
        mcMin = std::min(mcMin, mc.v[i]);
    }
    check(mcPeak > slPeak * 1.25f, "MacCormack keeps a moving pulse markedly sharper than semi-Lagrangian");
    check(mcPeak <= 1.0f + 1e-5f && mcMin >= -1e-6f, "and never creates a value the source did not hold");

    Grid flat(32, 32);
    std::fill(flat.v.begin(), flat.v.end(), 0.6f);
    const Grid moved = advectMacCormack(flat, 0.8f, -0.4f);
    bool same = true;
    for (float f : moved.v) same = same && std::fabs(f - 0.6f) < 1e-6f;
    check(same, "a constant field survives advection exactly");
}

// ---- Splats, heat, particles ------------------------------------------------------------------------

void splatFalloffAndCutoff() {
    check(std::fabs(math::splatFalloff(0.0f, 0.1f, 0.0f) - 1.0f) < 1e-6f, "a splat is full strength on its segment at the tail");
    check(std::fabs(math::splatFalloff(0.0f, 0.1f, 1.0f) - 0.4f) < 1e-6f, "and tapers to 40% at the head");
    check(math::splatFalloff(0.1f * math::kSplatCutoffRadii + 1e-3f, 0.1f, 0.0f) == 0.0f, "past the cutoff it is skipped");
    check(std::exp(-math::kSplatCutoffRadii) < 1.1e-5f, "and what the cutoff drops is invisible in half float");
    const auto [d, fp] = math::segDist(0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.3f);
    check(std::fabs(d - 0.3f) < 1e-6f && std::fabs(fp - 0.5f) < 1e-6f, "segment distance and the position along it");
}

void heatIsBoundedAndARate() {
    check(math::heatInjection(1.0f, 0.0f, 1.0f / 60.0f) == 0.0f, "no heat gain, no heat");
    const float at60 = math::heatInjection(1.0f, 1.0f, 1.0f / 60.0f);
    const float at120 = math::heatInjection(1.0f, 1.0f, 1.0f / 120.0f);
    check(std::fabs(at60 - 2.0f * at120) < 1e-6f, "heat is a rate: the same per second at any frame rate");
    check(math::heatHeadroom(0.0f) == 1.0f && math::heatHeadroom(math::kMaxTemperature) == 0.0f, "headroom runs out at the ceiling");
    check(math::coolingDivisor(0.5f, 0.1f) > 1.0f && math::coolingDivisor(0.0f, 0.1f) == 1.0f, "cooling divides, zero is none");
    check(math::buoyancyAccel(2.0f, 10.0f) == 2.0f * math::kMaxTemperature, "lift is bounded by the ceiling");
}

void drivesBlendSeveralSounds() {
    const GeodeSoundFrame quiet{};
    const math::Drives q = math::drives(quiet);
    check(q.heat > 0.0f && q.particleStir > 0.0f && q.stirRate > 0.0f && q.buoyancy > 0.0f && q.streak > 0.0f,
          "silence still moves every drive (a floor, not a gate)");
    // No drive belongs to one event: each must answer to at least two different readings.
    GeodeSoundFrame kick{};
    kick.kickEnv = 1.0f;
    GeodeSoundFrame bass{};
    bass.bassAtt = 2.0f;
    check(math::drives(kick).heat > q.heat && math::drives(bass).heat > q.heat, "heat hears the kick and the bass");
    GeodeSoundFrame hats{};
    hats.hatEnv = 1.0f;
    GeodeSoundFrame noise{};
    noise.flatness = 1.0f;
    check(math::drives(hats).particleStir > q.particleStir && math::drives(noise).particleStir > q.particleStir, "stir hears hats and noise");
    GeodeSoundFrame mids{};
    mids.midAtt = 2.0f;
    GeodeSoundFrame flux{};
    flux.flux = 2.0f;
    check(math::drives(mids).stirRate > q.stirRate && math::drives(flux).stirRate > q.stirRate, "stir rate hears mids and flux");
    GeodeSoundFrame onset{};
    onset.onsetEnv = 1.0f;
    GeodeSoundFrame loud{};
    loud.rms = 1.0f;
    check(math::drives(onset).streak > q.streak && math::drives(loud).streak > q.streak, "streaks hear onsets and level");
    GeodeSoundFrame all{};
    all.kickEnv = all.hatEnv = all.flatness = all.onsetEnv = all.snareEnv = all.rms = all.densitySlow = all.runDensity = 1.0f;
    all.bassAtt = all.midAtt = all.trebleAtt = all.subAtt = 3.0f;
    all.flux = 4.0f;
    all.energyTrend = 1.0f;
    const math::Drives a = math::drives(all);
    check(a.heat < 1.6f && a.particleStir < 1.6f && a.stirRate < 1.6f && a.buoyancy < 1.6f && a.streak < 1.6f, "every drive is bounded");
    GeodeSoundFrame broken{};
    broken.bassAtt = NAN;
    broken.kickEnv = INFINITY;
    check(std::isfinite(math::drives(broken).heat), "a non-finite reading cannot poison a drive");
}

void particleExposureKeepsLightSane() {
    check(math::particleExposure(curl::kRefParticles, curl::kRefParticles) == 1.0f, "the tuned population is unchanged");
    const int big = 640 * 640;
    const float e = math::particleExposure(big, curl::kRefParticles);
    check(e < 1.0f, "a bigger population draws each tracer dimmer");
    const float total = e * static_cast<float>(big) / static_cast<float>(curl::kRefParticles);
    check(total > 1.0f && total < 2.0f, "and adds detail and a little light, not a white-out");
}

void rk2HoldsAVortexOrbit() {
    // Solid-body rotation: an ideal tracer stays on its circle.
    const auto vortex = [](float x, float y) { return std::make_pair(-y * 3.0f, x * 3.0f); };
    float ex = 0.5f;
    float ey = 0.0f;
    float evx = 0.0f;
    float evy = 1.5f;
    float rx = ex;
    float ry = ey;
    float rvx = evx;
    float rvy = evy;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; ++i) {
        const auto [fx, fy] = vortex(ex, ey);
        evx = math::dragStep(evx, fx, 1.0f, dt);
        evy = math::dragStep(evy, fy, 1.0f, dt);
        ex += evx * dt;
        ey += evy * dt;
        math::tracerStepRk2(rx, ry, rvx, rvy, 1.0f, dt, vortex);
    }
    const float eulerDrift = std::fabs(std::hypot(ex, ey) - 0.5f);
    const float rk2Drift = std::fabs(std::hypot(rx, ry) - 0.5f);
    check(rk2Drift < eulerDrift * 0.1f, "the midpoint step holds a vortex orbit Euler spirals out of");
}

void tiersDescendAndKeepTheLowEndCheap() {
    for (int i = 1; i < quality::kTierCount; ++i) {
        const auto& a = quality::tier(i - 1);
        const auto& b = quality::tier(i);
        check(a.simRes >= b.simRes && a.dyeRes >= b.dyeRes && a.particleSide >= b.particleSide && a.curlParticleSide >= b.curlParticleSide,
              "every tier is at most the one above it");
        check(a.maxStretch >= b.maxStretch, "streaks only shorten down the ladder");
    }
    const auto& low = quality::tier(quality::kTierCount - 1);
    check(!low.macCormack && low.maxStretch == 0.0f, "the lowest tier keeps first-order advection and plain points");
    check(quality::tier(0).curlParticleSide * quality::tier(0).curlParticleSide >= 256 * 1024, "Ultra Curl Flow is a quarter-million tracers or more");
    check(quality::tier(0).dyeRes % 4 == 0 && quality::tier(0).dyeRes / quality::tier(0).simRes == 4, "dye stays four times the sim grid");
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// #383: a frame's simulated time is all of its dt, split into steps the fluid is stable under.
void substepsKeepTheFluidAtTrueSpeed() {
    using math::kMaxSimStep;
    using math::substeps;
    const auto at60 = substeps(1.0f / 60.0f);
    check(at60.count == 1 && near(at60.step, 1.0f / 60.0f, 1e-7f), "60 fps is one step of 1/60 s");
    const auto at30 = substeps(1.0f / 30.0f);
    check(at30.count == 1 && near(at30.step, kMaxSimStep, 1e-7f), "30 fps is one step at the limit");
    const auto at24 = substeps(1.0f / 24.0f);
    check(at24.count == 2 && near(at24.step * 2.0f, 1.0f / 24.0f, 1e-6f), "24 fps is two steps covering the whole frame");
    const auto at25 = substeps(1.0f / 25.0f);
    check(at25.count == 2 && near(at25.step * 2.0f, 1.0f / 25.0f, 1e-6f), "25 fps is two steps covering the whole frame");
    const auto stall = substeps(1.0f);
    check(stall.count == math::kMaxSubsteps && near(stall.step, kMaxSimStep, 1e-6f), "a stall is capped");
    check(substeps(0.0f).step == 0.0f && substeps(-1.0f).step == 0.0f && substeps(NAN).step == 0.0f, "no time, no step");
    for (int fps : {24, 25, 30, 50, 60}) {
        const auto s = substeps(1.0f / static_cast<float>(fps));
        check(s.step <= kMaxSimStep + 1e-7f, "every step stays inside the stable range");
        check(near(s.step * static_cast<float>(s.count), 1.0f / static_cast<float>(fps), 1e-6f), "and the frame is simulated in full");
    }
}

// #415 review: mutants of frameSimDt / substeps the checks above let through.
void frameTimeSplitsAtTheEdges() {
    using math::frameSimDt;
    using math::kMaxSimStep;
    using math::substeps;
    // Kills: frameSimDt clamping to one step again (the pre-#383 rule) and the cap's factor off by one.
    check(frameSimDt(1.0f / 24.0f) == 1.0f / 24.0f, "a 24 fps frame keeps all its time");
    check(near(frameSimDt(INFINITY), 4.0f / 30.0f, 1e-6f) && near(frameSimDt(0.14f), 4.0f / 30.0f, 1e-6f),
          "past four steps the frame is capped, an infinite one included");
    // Kills: the "hair under 1" in substeps dropped: a 30 fps frame that the live clock measures a touch long
    // (here 0.005 %) must stay one step, not split into two half steps.
    const auto longThirty = substeps(kMaxSimStep * 1.00005f);
    check(longThirty.count == 1, "a 30 fps frame a hair long stays one step");
    // Kills: ceil turned into floor or round: 20 and 10 fps need 2 and 3 steps.
    const auto at20 = substeps(1.0f / 20.0f);
    check(at20.count == 2 && near(at20.step, 0.025f, 1e-6f), "20 fps is two steps of 25 ms");
    const auto at10 = substeps(0.1f);
    check(at10.count == 3 && near(at10.step, kMaxSimStep, 1e-6f), "10 fps is three full steps");
    // Exactly four steps' worth is four steps; high refresh rates are one short step each.
    check(substeps(4.0f * kMaxSimStep).count == math::kMaxSubsteps, "the cap itself is four steps");
    for (int fps : {90, 120, 144}) {
        const auto s = substeps(1.0f / static_cast<float>(fps));
        check(s.count == 1 && near(s.step, 1.0f / static_cast<float>(fps), 1e-7f), "a high refresh frame is one step of its own length");
    }
    // Kills: the lower bound of the step count (1) lowered to 0: a tiny frame is one tiny step, never 0/0.
    const auto tiny = substeps(1e-6f);
    check(tiny.count == 1 && near(tiny.step, 1e-6f, 1e-9f), "a tiny frame is one tiny step");
}

}  // namespace

// A fluid scene's auto quality over a long session: a hot spell's deficits step it down, and once the
// phone keeps up again it climbs back to the user's tier instead of keeping Min for the scene's life.
void autoQualityRecoversAfterAHotSpell() {
    constexpr float dt = 1.0f / 60.0f;
    constexpr int userTier = 2;
    quality::AutoDowngrade a;
    a.onFrame(2, false, dt);
    a.onFrame(2, false, dt);
    check(quality::effectiveIndex(userTier, a.steps) == quality::kTierCount - 1, "a hot spell steps a fluid scene down to Min");
    check(a.steps == quality::kTierCount - 1, "the rungs stop at the ladder's end, so a step up is never swallowed");
    for (int i = 0; i < 60 * 600; ++i) a.onFrame(0, true, dt);
    check(quality::effectiveIndex(userTier, a.steps) == userTier, "ten kept-up minutes give the user's tier back");

    quality::AutoDowngrade idle;
    for (int i = 0; i < 60 * 600; ++i) idle.onFrame(0, false, dt);
    check(idle.steps == 0, "nothing to give back, nothing moves");

    quality::AutoDowngrade heavy;
    heavy.onFrame(1, false, dt);
    float t = 0.0f;
    while (heavy.steps == 1 && t < 100.0f) {
        heavy.onFrame(0, true, dt);
        t += dt;
    }
    check(heavy.steps == 0 && t >= quality::AutoDowngrade::kRelaxSeconds - 0.1f, "one rung back after the relax wait");
    heavy.onFrame(1, false, dt);  // the better tier failed inside the probe
    check(heavy.relaxSeconds == 2.0f * quality::AutoDowngrade::kRelaxSeconds, "a failed step up doubles the next wait");
    for (int k = 0; k < 10; ++k) {
        while (heavy.steps == 1) heavy.onFrame(0, true, dt);
        heavy.onFrame(1, false, dt);
    }
    check(heavy.relaxSeconds == quality::AutoDowngrade::kRelaxMaxSeconds, "the wait stops at its ceiling");
    while (heavy.steps == 1) heavy.onFrame(0, true, dt);
    for (int i = 0; i < 60 * 20; ++i) heavy.onFrame(0, true, dt);
    check(heavy.relaxSeconds == quality::AutoDowngrade::kRelaxSeconds, "a step up that holds puts the wait back");
}

int main() {
    autoQualityRecoversAfterAHotSpell();
    multigridLevelsCoarsenCellCentred();
    solvePlanSpendsTheBudget();
    multigridBeatsJacobiAtEveryBudget();
    macCormackCorrectorClamps();
    macCormackKeepsAPulseSharpWithoutOvershoot();
    splatFalloffAndCutoff();
    heatIsBoundedAndARate();
    drivesBlendSeveralSounds();
    particleExposureKeepsLightSane();
    rk2HoldsAVortexOrbit();
    tiersDescendAndKeepTheLowEndCheap();
    substepsKeepTheFluidAtTrueSpeed();
    frameTimeSplitsAtTheEdges();
    if (failures == 0) std::printf("fluid_math_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
