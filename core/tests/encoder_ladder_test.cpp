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
    for (int fps : {24, 25, 30, 50, 60}) {
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
            check(any.rungs[any.count - 1].fps >= out::kLadderMinFps, "never below 24 fps");
            check(any.rungs[any.count - 1].fps <= out::kLadderFloorFps, "the last rung is at or below 30");
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

void cAbiMatchesTheHeader() {
    int fps[4] = {};
    int bps[4] = {};
    check(geode_encoder_ladder(60, 12'000'000, 0, fps, bps, 4) == 2, "ABI: two rungs");
    check(fps[0] == 60 && bps[0] == 12'000'000 && fps[1] == 30 && bps[1] == 8'000'000, "ABI: the rungs");
    check(geode_encoder_ladder(60, 12'000'000, 30, fps, bps, 4) == 1 && fps[0] == 30, "ABI: the ceiling");
    check(geode_encoder_ladder(60, 12'000'000, 0, fps, bps, 1) == 1 && fps[0] == 60, "ABI: capacity caps");
    check(geode_encoder_ladder(60, 12'000'000, 0, nullptr, bps, 4) == 0, "ABI: NULL fps");
    check(geode_encoder_ladder(60, 12'000'000, 0, fps, nullptr, 4) == 0, "ABI: NULL bit rates");
    check(geode_encoder_ladder(60, 12'000'000, 0, fps, bps, 0) == 0, "ABI: no room");
}

}  // namespace

int main() {
    fallbackStaysInTheCadenceFamily();
    unknownCeilingOffersBothRungs();
    requestIsClamped();
    ceilingSkipsRungsAboveIt();
    floorIsAlwaysTried();
    bitRateScaling();
    cAbiMatchesTheHeader();
    if (failures == 0) std::printf("encoder_ladder_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
