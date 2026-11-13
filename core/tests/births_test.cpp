// Life's births (viz/Births): nothing is born at Life 0, the same clock always gives the same births, every
// collapse kind comes round, Life brings the births in and pushes their drift, and the clock is the only input.
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <type_traits>

#include "viz/Births.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

int present(const std::array<float, births::kFloats>& v) {
    int n = 0;
    for (int b = 0; b < births::kBirths; ++b) n += v[8 * b + 3] > 0.0f ? 1 : 0;
    return n;
}

}  // namespace

int main() {
    // Life 0: every value exactly 0 at any clock, so the shaders' birth passes are all skipped.
    for (float t : {0.0f, 1.3f, 77.0f, 4000.0f, 7099.9f}) {
        auto v = births::compute(t, 0.0f);
        bool zero = true;
        for (float x : v) zero = zero && x == 0.0f;
        check(zero, "Life 0 bears nothing at t " + std::to_string(t));
    }

    // Deterministic: the same clock and Life give the same births, bit for bit (export draws what live drew).
    for (float t : {0.5f, 93.25f, 1741.0f}) {
        check(births::compute(t, 0.7f) == births::compute(t, 0.7f), "the same clock gives the same births");
    }

    // The signature takes the clock and Life alone: no level, band, beat or tempo can reach it.
    static_assert(std::is_same_v<decltype(&births::compute), std::array<float, births::kFloats> (*)(float, float)>,
                  "compute takes the clock and Life only");

    // Every kind comes round, in every slot, over a few lives (so even one birth at a little Life dies all three ways).
    std::set<int> kinds;
    std::set<int> slotKinds[births::kBirths];
    for (int i = 0; i < 400; ++i) {
        float t = 0.137f * static_cast<float>(i) * births::kPeriod;
        auto v = births::compute(t, 1.0f);
        for (int b = 0; b < births::kBirths; ++b) {
            int k = static_cast<int>(v[8 * b + 4]);
            check(k >= 0 && k <= 2, "a kind is Implode, Fold back or Cave in");
            kinds.insert(k);
            slotKinds[b].insert(k);
        }
    }
    check(kinds.size() == 3, "Implode, Fold back and Cave in all appear");
    for (int b = 0; b < births::kBirths; ++b) check(slotKinds[b].size() == 3, "each slot takes every kind in turn");

    // A life: born from nothing, grown, then collapsed by its end; collapse 0 while growing.
    {
        float start = 3.0f * births::kPeriod;  // slot 0 is born on a whole period
        auto born = births::compute(start, 1.0f);
        check(born[3] == 0.0f, "a birth starts from nothing");
        auto grown = births::compute(start + 0.4f * births::kPeriod, 1.0f);
        check(std::fabs(grown[3] - births::kMaxScale) < 1e-5f, "a grown birth is full size");
        check(grown[5] == 0.0f, "it has not started to collapse while drifting");
        auto dying = births::compute(start + 0.999f * births::kPeriod, 1.0f);
        check(dying[5] > 0.99f, "it has all but collapsed by the end of its life");
    }

    // Life brings them in one by one and nothing pops: a third of the dial each.
    float mid = 41.0f + 0.4f * births::kPeriod;
    check(present(births::compute(mid, 0.2f)) >= 1, "a little Life bears one");
    check(present(births::compute(41.0f, 1.0f)) >= 2, "full Life bears all three in turn");
    {
        auto a = births::compute(mid, 0.34f);
        auto b = births::compute(mid, 0.3334f);
        check(std::fabs(a[8 + 3] - b[8 + 3]) < 0.01f, "the second birth fades in, never pops");
    }

    // Life pushes the drift: the same life drifts farther from its spawn point at more Life.
    {
        float start = 11.0f * births::kPeriod;
        auto at0 = births::compute(start + 0.01f, 1.0f);
        auto lo = births::compute(start + 0.7f * births::kPeriod, 0.4f);
        auto hi = births::compute(start + 0.7f * births::kPeriod, 1.0f);
        float dlo = std::hypot(lo[0] - at0[0], lo[1] - at0[1]);
        float dhi = std::hypot(hi[0] - at0[0], hi[1] - at0[1]);
        check(dhi > dlo, "more Life drifts farther");
        // Births are born off the centre and stay within the reach the shaders' slack is built from.
        check(std::hypot(at0[0], at0[1]) >= 0.79f, "a birth is born off the centre");
        for (int i = 0; i < 2000; ++i) {
            auto v = births::compute(3.7f * static_cast<float>(i), 1.0f);
            for (int b = 0; b < births::kBirths; ++b) {
                float r = std::sqrt(v[8 * b] * v[8 * b] + v[8 * b + 1] * v[8 * b + 1] + v[8 * b + 2] * v[8 * b + 2]);
                if (!(r <= births::kMaxReach)) check(false, "a birth stays within kMaxReach");
                if (!(v[8 * b + 3] <= births::kMaxScale)) check(false, "a birth is never larger than kMaxScale");
            }
        }
    }

    // The clock's wrap at 7100 s lands on a life's end: the births just after it are the births just after 0.
    {
        auto a = births::compute(2.5f, 0.9f);
        auto b = births::compute(7100.0f + 2.5f, 0.9f);
        bool same = true;
        for (int i = 0; i < births::kFloats; ++i) same = same && std::fabs(a[i] - b[i]) < 1e-3f;
        check(same, "the 7100 s wrap is seamless");
    }

    if (failures == 0) std::printf("births_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
