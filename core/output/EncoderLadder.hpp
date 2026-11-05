#pragma once

// The encoder fallback ladder (#607): which frame rate and bit rate an export asks the video encoder
// for, rung by rung, until MediaCodec.configure accepts one. It used to be two Kotlin copies
// (export/LoopRender `negotiate`, export/VideoExporter's attempt list, both over data/ExportPrefs
// `fallbackExportFps`); one source here keeps the loop render and the one-tap render from drifting.
//
// Kotlin still asks the platform what the encoder can do (MediaCodecInfo.VideoCapabilities
// .getSupportedFrameRatesFor, the coding standard's limit for the size, not a performance promise)
// and hands the upper bound in as `ceilingFps`. It still tries each rung with configure, because the
// capability table can be wrong in either direction; the ceiling only skips a rung the table says
// is out of reach, so a 4K60 request on an encoder that lists 30 does not pay a refused configure.
//
// The rules, unchanged from the Kotlin:
// - The request is clamped to the rates any export offers, 24..60.
// - Rung 0 is the request at the full bit rate.
// - Rung 1 is the fallback: half the rate above 30 fps (50 -> 25, 60 -> 30, so the retry stays in the
//   request's cadence family), the rate itself at 30 and below, at 2/3 of the bit rate. One halving
//   always lands at or below 30, the floor above which a rate is halved. It is not clamped again:
//   a request of 31..47 falls back to 15..23, below the 24 a request is clamped to, exactly as the
//   Kotlin's `requested / 2` did.
// - A ceiling of 0 or less is "unknown": every rung is offered.
// - A known ceiling drops the rungs above it, but never the last one: when even the floor is over
//   the ceiling, the floor is still tried, since configure, not the table, has the final word.
//
// Pure and header-only: core/tests/encoder_ladder_test.cpp builds it with the host compiler.

#include <algorithm>
#include <cstdint>

namespace geode::output {

inline constexpr int kLadderMinFps = 24;
inline constexpr int kLadderMaxFps = 60;
/** Rates at or below this are never halved, and every halving lands at or below it. */
inline constexpr int kLadderFloorFps = 30;
/** The most rungs encoderLadder writes. */
inline constexpr int kLadderMaxRungs = 2;

struct EncoderRung {
    int fps = 0;
    int bitRate = 0;
};

/** The rate a refused request retries at: half above the floor, the request itself at or below it. */
constexpr int fallbackFps(int fps) { return fps > kLadderFloorFps ? fps / 2 : fps; }

/** 2/3 of a bit rate, in 64-bit so a large rate cannot wrap; never negative. */
constexpr int reducedBitRate(int bitRate) {
    return bitRate > 0 ? static_cast<int>(static_cast<int64_t>(bitRate) * 2 / 3) : 0;
}

/**
 * Writes the ladder for a request into out[0 .. kLadderMaxRungs) in the order to try, and returns how
 * many rungs it wrote (1 or 2). `ceilingFps` <= 0 means the encoder did not say.
 */
constexpr int encoderLadder(int requestedFps, int bitRate, int ceilingFps, EncoderRung* out) {
    const int fps = std::clamp(requestedFps, kLadderMinFps, kLadderMaxFps);
    const int bps = std::max(bitRate, 0);
    const EncoderRung rungs[kLadderMaxRungs] = {{fps, bps}, {fallbackFps(fps), reducedBitRate(bps)}};
    int written = 0;
    for (int i = 0; i < kLadderMaxRungs; ++i) {
        const bool floorRung = i == kLadderMaxRungs - 1;
        if (ceilingFps > 0 && rungs[i].fps > ceilingFps && !floorRung) continue;
        out[written++] = rungs[i];
    }
    return written;
}

}  // namespace geode::output
