// Host-side checks for a ring's UI level (core/audio/ingest/Level.hpp). Built and run by CI ("Native
// host tests" in .github/workflows/android.yml) with the system compiler; no ring, no device.
//
// What is pinned: the level is the RMS of the newest samples while the ring is being written; a ring
// that stops being written reads 0 after kStaleMs, so a paused tap never freezes the edge glow wide
// open; the first poll treats what was written before anyone listened as old news. The ring-backed
// answers (geode_ingest_resting and geode_ingest_level through the C API) are pinned in ingest_test.cpp.
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/ingest/Level.hpp"

namespace level = geode::audio::ingest::level;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// A fake ring: a count and a newest-first tail of one constant-amplitude sine.
struct FakeRing {
    long long written = 0;
    float amplitude = 0.0f;
    int available = level::kWindowFrames;  // how much the tail can give

    int tail(float* out, int n) const {
        const int k = n < available ? n : available;
        for (int i = 0; i < k; ++i) out[i] = amplitude * static_cast<float>(std::sin(0.2 * i));
        return k;
    }
};

float poll(level::Meter& m, const FakeRing& r, int64_t now) {
    return m.poll(r.written, now, [&r](float* out, int n) { return r.tail(out, n); });
}

void rmsOfASine() {
    std::vector<float> s(4800);
    for (size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
    check(near(level::rms(s.data(), static_cast<int>(s.size())), 0.5f / std::sqrt(2.0f), 0.005f),
          "a 0.5 sine reads its RMS, 0.354");
    check(level::rms(nullptr, 10) == 0.0f && level::rms(s.data(), 0) == 0.0f, "nothing reads 0");
}

void levelFollowsWrites() {
    level::Meter m;
    FakeRing r{48000, 0.5f, level::kWindowFrames};
    check(poll(m, r, 0) == 0.0f, "the first poll: what was written before anyone listened is old news");
    r.written += 480;
    const float l = poll(m, r, 10);
    check(l > 0.3f && l < 0.4f, "new writes: the newest samples' RMS");
    check(near(poll(m, r, 10 + level::kStaleMs), l, 1e-6f), "still within the stale window: the same level");
    check(poll(m, r, 11 + level::kStaleMs) == 0.0f, "no writes for longer than kStaleMs: 0 (a paused tap)");
    r.written += 480;
    check(poll(m, r, 500) > 0.3f, "writes again: the level comes back on the next poll");
    r.amplitude = 0.0f;
    r.written += 480;
    check(poll(m, r, 520) == 0.0f, "written silence reads 0");
}

void shortTailAndEmptyRing() {
    level::Meter m;
    FakeRing r{0, 0.5f, 0};
    poll(m, r, 0);
    check(poll(m, r, 5) == 0.0f, "a ring never written reads 0");
    r.written = 64;
    r.available = 64;
    const float l = poll(m, r, 10);
    check(l > 0.2f && l < 0.45f, "a tail shorter than the window: the RMS of what there is");
}

}  // namespace

int main() {
    rmsOfASine();
    levelFollowsWrites();
    shortTailAndEmptyRing();
    if (failures == 0) std::printf("ingest_level_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
