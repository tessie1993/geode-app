// Host-side checks for core/output/EncoderLadder (#607): the frame rates and bit rates an export asks the
// video encoder for, which used to be Kotlin (export/LoopRender `negotiate`, export/VideoExporter, both over
// data/ExportPrefs `fallbackExportFps`). Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler, through the C ABI as well as the header.
//
// What is pinned: with no ceiling the rungs equal the Kotlin's two attempts exactly (the request clamped to
// 24..60 at the full rate, then the cadence-family fallback at 2/3), and a known ceiling only skips rungs.
#include <cstdio>

#include "api/geode_api.h"
#include "output/EncoderLadder.hpp"

namespace out = geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Ladder {
    int count = 0;
    out::EncoderRung rungs[out::kLadderMaxRungs];
};

Ladder ladder(int fps, int bitRate, int ceiling) {
    Ladder l;
    l.count = out::encoderLadder(fps, bitRate, ceiling, l.rungs);
    return l;
}

bool rung(const Ladder& l, int i, int fps, int bitRate) {
    return i < l.count && l.rungs[i].fps == fps && l.rungs[i].bitRate == bitRate;
}

// The Kotlin it replaces: `if (requested > 30) requested / 2 else requested`.
int kotlinFallback(int requested) { return requested > 30 ? requested / 2 : requested; }

void fallbackStaysInTheCadenceFamily() {
    check(out::fallbackFps(24) == 24, "24 retries at 24");
    check(out::fallbackFps(25) == 25, "25 retries at 25");
    check(out::fallbackFps(30) == 30, "30 retries at 30");
    check(out::fallbackFps(50) == 25, "50 retries at 25, not 30");
    check(out::fallbackFps(60) == 30, "60 retries at 30");
    // Between 31 and 47 the Kotlin halved below the 24 a request is clamped to, and nothing clamped
    // the result again: the fallback keeps that, so the ladder matches what shipped.
    check(out::fallbackFps(31) == 15, "31 retries at 15, as the Kotlin did");
    check(out::fallbackFps(40) == 20, "40 retries at 20, as the Kotlin did");
    check(out::fallbackFps(47) == 23, "47 retries at 23, as the Kotlin did");
    check(out::fallbackFps(48) == 24, "48 retries at 24");
    for (int fps = out::kLadderMinFps; fps <= out::kLadderMaxFps; ++fps) {
        check(out::fallbackFps(fps) <= fps, "a fallback never raises the rate");
        check(out::fallbackFps(fps) == kotlinFallback(fps), "fallback equals the Kotlin");
    }
}

void unknownCeilingOffersBothRungs() {
    for (int ceiling : {0, -1}) {
        const Ladder l = ladder(60, 40'000'000, ceiling);
        check(l.count == 2, "unknown ceiling: two rungs");
        check(rung(l, 0, 60, 40'000'000), "unknown ceiling: the request at full rate first");
        check(rung(l, 1, 30, 26'666'666), "unknown ceiling: half the rate at 2/3 of the bits");
    }
    const Ladder low = ladder(24, 6'000'000, 0);
    check(rung(low, 0, 24, 6'000'000) && rung(low, 1, 24, 4'000'000), "24 fps keeps its rate and drops bits");
}

void requestIsClamped() {
    check(rung(ladder(120, 1'000, 0), 0, 60, 1'000), "above 60 clamps to 60");
    check(rung(ladder(120, 1'000, 0), 1, 30, 666), "the clamped request halves");
    check(rung(ladder(0, 1'000, 0), 0, 24, 1'000), "below 24 clamps to 24");
    check(rung(ladder(-5, 1'000, 0), 1, 24, 666), "a bad request lands on the floor");
}

void ceilingSkipsRungsAboveIt() {
    // The encoder lists 30 at this size: a 60 request goes straight to the halved rung.
    const Ladder sixty = ladder(60, 12'000'000, 30);
    check(sixty.count == 1, "ceiling 30: one rung for 60");
    check(rung(sixty, 0, 30, 8'000'000), "ceiling 30: 60 halves and drops to 2/3");
    const Ladder fifty = ladder(50, 12'000'000, 30);
    check(fifty.count == 1 && rung(fifty, 0, 25, 8'000'000), "ceiling 30: 50 halves to 25");
    // A ceiling at or above the request leaves the ladder whole: configure can still refuse.
    const Ladder roomy = ladder(60, 12'000'000, 60);
    check(roomy.count == 2 && rung(roomy, 0, 60, 12'000'000) && rung(roomy, 1, 30, 8'000'000),
          "ceiling 60: both rungs");
    const Ladder high = ladder(30, 12'000'000, 240);
    check(high.count == 2 && rung(high, 0, 30, 12'000'000) && rung(high, 1, 30, 8'000'000),
          "a high ceiling: both rungs");
}

void floorIsAlwaysTried() {
    // Even the floor is over the ceiling: it is still offered, since the table is not the final word.
    const Ladder l = ladder(60, 9'000'000, 20);
    check(l.count == 1 && rung(l, 0, 30, 6'000'000), "ceiling 20: the floor rung for 60");
    const Ladder low = ladder(24, 9'000'000, 20);
    check(low.count == 1 && rung(low, 0, 24, 6'000'000), "ceiling 20: 24 at reduced bits");
    for (int fps = -10; fps <= 200; ++fps) {
        for (int ceiling : {-1, 0, 1, 24, 25, 30, 50, 60, 1000}) {
            const Ladder any = ladder(fps, 1'000'000, ceiling);
            check(any.count >= 1 && any.count <= out::kLadderMaxRungs, "one or two rungs");
            // The last rung is always the Kotlin's fallback of the clamped request (which can be below
            // 24 for a 31..47 request), at 2/3 of the bits, whatever the ceiling says.
            const int clamped = fps < out::kLadderMinFps ? out::kLadderMinFps
                                : fps > out::kLadderMaxFps ? out::kLadderMaxFps
                                                           : fps;
            check(any.rungs[any.count - 1].fps == kotlinFallback(clamped), "the last rung is the Kotlin's fallback");
            check(any.rungs[any.count - 1].bitRate == 666'666, "the last rung is at 2/3 of the bits");
            check(any.rungs[any.count - 1].fps <= out::kLadderFloorFps, "the last rung is at or below 30");
            check(any.rungs[0].fps == clamped || any.count == 1, "a full ladder starts at the clamped request");
        }
    }
}

void bitRateScaling() {
    check(out::reducedBitRate(3) == 2, "3 -> 2");
    check(out::reducedBitRate(40'000'000) == 26'666'666, "2/3 rounds down like Kotlin's Int division");
    check(out::reducedBitRate(2'147'483'647) == 1'431'655'764, "no overflow at Int.MAX_VALUE");
    check(out::reducedBitRate(0) == 0, "zero stays zero");
    check(out::reducedBitRate(-9) == 0, "never negative");
    check(rung(ladder(60, -1, 0), 0, 60, 0), "a negative rate is clamped to zero");
}

void attemptsWalkEachCodecsLadder() {
    out::EncoderAttempt a[out::kAttemptMaxCount];
    const int both[2] = {0, 0};
    check(out::encoderAttempts(60, 12'000'000, both, 2, a) == 4, "two codecs, two rungs each");
    check(a[0].codec == 0 && a[1].codec == 0 && a[2].codec == 1 && a[3].codec == 1, "codec 0's ladder first");
    check(a[2].rung.fps == 60 && a[3].rung.fps == 30 && a[3].rung.bitRate == 8'000'000, "codec 1 repeats it");
    const int ceilings[2] = {30, 0};
    check(out::encoderAttempts(60, 12'000'000, ceilings, 2, a) == 3, "each codec under its own ceiling");
    check(a[0].codec == 0 && a[0].rung.fps == 30 && a[1].codec == 1 && a[1].rung.fps == 60, "the ceiling skips");
    check(out::encoderAttempts(60, 12'000'000, both, 1, a) == 2, "one codec, its ladder alone");
    check(out::encoderAttempts(60, 12'000'000, both, 0, a) == 0, "no codecs, no attempts");
    const int three[3] = {0, 0, 0};
    check(out::encoderAttempts(60, 12'000'000, three, 3, a) == out::kAttemptMaxCount, "at most two codecs");
}

void cAbiMatchesTheHeader() {
    const int ceilings[2] = {0, 30};
    int codec[4] = {};
    int fps[4] = {};
    int bps[4] = {};
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, codec, fps, bps, 4) == 3, "ABI: three attempts");
    check(codec[0] == 0 && fps[0] == 60 && bps[0] == 12'000'000, "ABI: codec 0 at the request");
    check(codec[1] == 0 && fps[1] == 30 && bps[1] == 8'000'000, "ABI: codec 0's fallback");
    check(codec[2] == 1 && fps[2] == 30 && bps[2] == 8'000'000, "ABI: codec 1 under its ceiling");
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, codec, fps, bps, 1) == 1 && fps[0] == 60,
          "ABI: capacity caps");
    check(geode_encoder_attempts(60, 12'000'000, nullptr, 2, codec, fps, bps, 4) == 0, "ABI: NULL ceilings");
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, nullptr, fps, bps, 4) == 0, "ABI: NULL codecs");
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, codec, nullptr, bps, 4) == 0, "ABI: NULL fps");
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, codec, fps, nullptr, 4) == 0, "ABI: NULL bit rates");
    check(geode_encoder_attempts(60, 12'000'000, ceilings, 2, codec, fps, bps, 0) == 0, "ABI: no room");
}

}  // namespace

int main() {
    fallbackStaysInTheCadenceFamily();
    unknownCeilingOffersBothRungs();
    requestIsClamped();
    ceilingSkipsRungsAboveIt();
    floorIsAlwaysTried();
    bitRateScaling();
    attemptsWalkEachCodecsLadder();
    cAbiMatchesTheHeader();
    if (failures == 0) std::printf("encoder_ladder_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
