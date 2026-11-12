// Host-side checks for the two hidden Visuals gestures: shake to screenshot's detector (core/viz/Shake, through
// its C ABI) and tap along's matcher (core/viz/TapAlong). Built and run by CI
// (.github/workflows/native-host-tests.yml, "Easter egg host tests") with the system compiler; no GL, no device.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "api/geode_api.h"
#include "viz/Shake.hpp"
#include "viz/TapAlong.hpp"

using geode::viz::TapAlong;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 0.02;  // SENSOR_DELAY_GAME
constexpr float kG = 9.81f;

// Deterministic noise in -1..1.
struct Lcg {
    uint64_t s;
    double next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(s >> 11) / static_cast<double>(1ull << 53) * 2.0 - 1.0;
    }
};

// Feeds `seconds` of motion at 50 Hz from t0 and counts the shakes. motion(t) adds to a phone held upright.
template <typename F>
int shakes(geode_shake* s, double t0, double seconds, F motion) {
    int fired = 0;
    for (double t = t0; t < t0 + seconds; t += kDt) {
        float x = 0.0f, y = kG, z = 0.0f;
        motion(t, x, y, z);
        fired += geode_shake_sample(s, static_cast<int64_t>(t * 1e9), x, y, z);
    }
    return fired;
}

void shakeTests() {
    geode_shake* s = geode_shake_create();
    Lcg noise{7};
    const auto still = [&](double, float& x, float& y, float& z) {
        x += 0.3f * static_cast<float>(noise.next());
        y += 0.3f * static_cast<float>(noise.next());
        z += 0.3f * static_cast<float>(noise.next());
    };
    check(shakes(s, 0.0, 5.0, still) == 0, "a phone held still never shakes");

    geode_shake_reset(s);
    // Walking: ~2 Hz steps, about half a g up and down, a little sway.
    const auto walk = [&](double t, float& x, float& y, float&) {
        y += 5.0f * static_cast<float>(std::sin(2.0 * kPi * 2.0 * t));
        x += 2.0f * static_cast<float>(std::sin(2.0 * kPi * 1.0 * t));
    };
    check(shakes(s, 10.0, 10.0, walk) == 0, "walking never shakes");

    geode_shake_reset(s);
    // Running: ~3 Hz, a g each way.
    const auto run = [&](double t, float&, float& y, float&) { y += 10.0f * static_cast<float>(std::sin(2.0 * kPi * 3.0 * t)); };
    check(shakes(s, 30.0, 10.0, run) == 0, "running under the threshold never shakes");

    geode_shake_reset(s);
    // Set down: turned flat over 0.4 s, then a thud ringing at the sample rate, then flat and still.
    const auto setDown = [&](double t, float& x, float& y, float& z) {
        const double u = std::fmin(std::fmax((t - 51.0) / 0.4, 0.0), 1.0);
        y = kG * static_cast<float>(std::cos(u * kPi / 2.0));
        z = kG * static_cast<float>(std::sin(u * kPi / 2.0));
        x = 0.0f;
        const double since = t - 51.42;
        if (since >= 0.0 && since < 0.12) z += static_cast<float>(30.0 * std::exp(-since / 0.04) * (static_cast<int>(since / kDt) % 2 ? -1 : 1));
    };
    check(shakes(s, 50.0, 3.0, setDown) == 0, "setting the phone down never shakes");

    geode_shake_reset(s);
    // Turned over fast, twice, as when picked up and looked at.
    const auto flip = [&](double t, float& x, float& y, float& z) {
        const double a = kPi * std::fmin(std::fmax((t - 61.0) / 0.3, 0.0), 1.0) - kPi * std::fmin(std::fmax((t - 62.0) / 0.3, 0.0), 1.0);
        y = kG * static_cast<float>(std::cos(a));
        z = kG * static_cast<float>(std::sin(a));
        x = 0.0f;
    };
    check(shakes(s, 60.0, 4.0, flip) == 0, "turning the phone over never shakes");

    geode_shake_reset(s);
    // A deliberate shake: 5 Hz side to side, 2 g, for one second.
    const auto shake = [&](double t, float& x, float&, float&) {
        if (t >= 71.0 && t < 72.0) x += 20.0f * static_cast<float>(std::sin(2.0 * kPi * 5.0 * (t - 71.0)));
    };
    check(shakes(s, 70.0, 4.0, shake) == 1, "a one second shake saves one picture");

    geode_shake_reset(s);
    // Shaken on for three seconds: still one picture.
    const auto longShake = [&](double t, float& x, float&, float&) {
        if (t >= 81.0 && t < 84.0) x += 20.0f * static_cast<float>(std::sin(2.0 * kPi * 4.0 * (t - 81.0)));
    };
    check(shakes(s, 80.0, 5.0, longShake) == 1, "a long shake saves one picture");

    geode_shake_reset(s);
    // Two shakes with a calm second and a half between them: two pictures.
    const auto twice = [&](double t, float& x, float&, float&) {
        if ((t >= 91.0 && t < 92.0) || (t >= 94.0 && t < 95.0)) x += 20.0f * static_cast<float>(std::sin(2.0 * kPi * 5.0 * t));
    };
    check(shakes(s, 90.0, 6.0, twice) == 2, "two shakes apart save two pictures");

    geode_shake_reset(s);
    // A small shake (half the threshold) never counts.
    const auto gentle = [&](double t, float& x, float&, float&) {
        if (t >= 101.0 && t < 103.0) x += 7.0f * static_cast<float>(std::sin(2.0 * kPi * 5.0 * t));
    };
    check(shakes(s, 100.0, 4.0, gentle) == 0, "a gentle shake never counts");

    check(geode_shake_sample(nullptr, 0, 0, 0, 0) == 0, "a null handle is a no");
    geode_shake_destroy(s);
}

// A song at bpm whose beat the engine tracks from t = 0 (beat at every whole period).
struct Song {
    double bpm;
    float conf = 0.8f;
    float presence = 1.0f;
    double period() const { return 60.0 / bpm; }
    // One frame at t (60 fps grid), then the tap at tap.
    bool tapAt(TapAlong& m, double tap) const {
        const double frameT = std::floor(tap * 60.0) / 60.0;
        const double phase = frameT / period() - std::floor(frameT / period());
        m.frame(frameT, static_cast<float>(phase), static_cast<float>(bpm), conf, presence);
        return m.tap(tap);
    }
};

int taps(TapAlong& m, const Song& song, const double* at, int n) {
    int fired = 0;
    for (int i = 0; i < n; ++i) fired += song.tapAt(m, at[i]) ? 1 : 0;
    return fired;
}

void tapTests() {
    const Song song{120.0};
    const double p = song.period();
    {
        TapAlong m;
        const double on[] = {10 * p + 0.01, 11 * p - 0.02, 12 * p + 0.0, 13 * p + 0.02};
        check(taps(m, song, on, 4) == 1, "four taps on the beat fire");
        check(!song.tapAt(m, 14 * p), "the fifth tap starts a new streak");
    }
    {
        TapAlong m;
        double on[8];
        for (int i = 0; i < 8; ++i) on[i] = (20 + i) * p + 0.03;
        check(taps(m, song, on, 8) == 2, "eight taps on the beat fire twice, once per four");
    }
    {
        TapAlong m;
        // Late by the speaker's latency, steadily: still the heard beat.
        const double late[] = {30 * p + 0.12, 31 * p + 0.11, 32 * p + 0.13, 33 * p + 0.12};
        check(taps(m, song, late, 4) == 1, "steadily late taps (output latency) fire");
    }
    {
        TapAlong m;
        const double half[] = {40 * p, 42 * p + 0.01, 44 * p - 0.01, 46 * p};
        check(taps(m, song, half, 4) == 1, "taps on every other beat fire");
    }
    {
        TapAlong m;
        const double off[] = {50 * p + p / 2, 51 * p + p / 2, 52 * p + p / 2, 53 * p + p / 2};
        check(taps(m, song, off, 4) == 0, "taps on the off beat never fire");
    }
    {
        TapAlong m;
        const double three[] = {60 * p, 61 * p, 62 * p, 63 * p + p / 2, 64 * p, 65 * p, 66 * p};
        check(taps(m, song, three, 7) == 0, "an off-beat tap breaks the streak");
    }
    {
        TapAlong m;
        const double mixed[] = {70 * p, 71 * p, 73 * p, 74 * p};
        check(taps(m, song, mixed, 4) == 0, "a changed stride never fires");
    }
    {
        TapAlong m;
        double dbl[8];
        for (int i = 0; i < 4; ++i) {
            dbl[2 * i] = (80 + i) * p;
            dbl[2 * i + 1] = (80 + i) * p + 0.15;
        }
        check(taps(m, song, dbl, 8) == 0, "double taps on each beat never fire");
    }
    {
        TapAlong m;
        double dbl[8];
        for (int i = 0; i < 4; ++i) {
            dbl[2 * i] = (90 + 2 * i) * p;
            dbl[2 * i + 1] = (90 + 2 * i) * p + 0.2;
        }
        check(taps(m, song, dbl, 8) == 0, "double taps on every other beat never fire");
    }
    {
        Song quiet = song;
        quiet.presence = 0.0f;
        TapAlong m;
        const double on[] = {100 * p, 101 * p, 102 * p, 103 * p};
        check(taps(m, quiet, on, 4) == 0, "taps in silence never fire");
        Song unsure = song;
        unsure.conf = 0.1f;
        check(taps(m, unsure, on, 4) == 0, "taps on a beat the tracker is unsure of never fire");
        Song none = song;
        none.bpm = 0.0;
        check(taps(m, none, on, 4) == 0, "taps with no tempo yet never fire");
        TapAlong fresh;
        check(fresh.tap(1.0) == false, "a tap before any frame is a no");
    }
    {
        TapAlong m;
        m.frame(0.0, 0.0f, 120.0f, 0.8f, 1.0f);
        check(!m.tap(5.0) && !m.tap(5.5) && !m.tap(6.0) && !m.tap(6.5), "taps against a stale frame never fire");
    }
    {
        // Taps the way the chrome is woken, seconds apart at random, against songs from 70 to 180 bpm: never.
        int fired = 0;
        for (uint64_t seed = 1; seed <= 200; ++seed) {
            Lcg r{seed};
            const Song any{70.0 + 110.0 * (r.next() * 0.5 + 0.5)};
            TapAlong m;
            double t = 10.0;
            for (int i = 0; i < 60; ++i) {
                t += 2.2 + 3.8 * (r.next() * 0.5 + 0.5);
                fired += any.tapAt(m, t) ? 1 : 0;
            }
        }
        check(fired == 0, "sparse random taps never fire (" + std::to_string(fired) + ")");
        // Dense random drumming on the screen (0.15 to 1.2 s apart): a fire is a 4-in-a-row fluke, held under
        // one per thousand taps.
        long dense = 0, total = 0;
        for (uint64_t seed = 1; seed <= 2000; ++seed) {
            Lcg r{seed};
            const Song any{70.0 + 110.0 * (r.next() * 0.5 + 0.5)};
            TapAlong m;
            double t = 10.0;
            for (int i = 0; i < 60; ++i, ++total) {
                t += 0.15 + 1.05 * (r.next() * 0.5 + 0.5);
                dense += any.tapAt(m, t) ? 1 : 0;
            }
        }
        check(dense * 1000 < total, "dense random taps fire under one per thousand (" + std::to_string(dense) + ")");
    }
    {
        // The matcher writes nothing back: the frame it was given is all it knows, so the same taps against
        // the same frames give the same answer however often they are replayed.
        TapAlong a, b;
        const double on[] = {110 * p, 111 * p, 112 * p, 113 * p};
        check(taps(a, song, on, 4) == taps(b, song, on, 4), "the matcher is a pure read of the heard beat");
    }
}

}  // namespace

int main() {
    shakeTests();
    tapTests();
    if (failures == 0) std::printf("easter_eggs_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
