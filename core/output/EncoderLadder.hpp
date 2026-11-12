#pragma once

// The encoder fallback ladder (#607): which frame rate and bit rate an export asks the video encoder
// for, rung by rung, until the encoder's configure accepts one. The loop render (output/LoopRender) and the
// one-tap render both walk this one ladder, so the two cannot drift apart.
//
// encoderAttempts gives the whole attempt list an export walks: each candidate codec's ladder in turn (the
// requested codec, then H.264), so the caller only walks it.
//
// The shell asks the platform what the encoder can do (MediaCodecInfo.VideoCapabilities
// .getSupportedFrameRatesFor, the coding standard's limit for the size, not a performance promise)
// and hands the upper bound in as `ceilingFps`. Each rung is still tried with configure, because the
// capability table can be wrong in either direction; the ceiling only skips a rung the table says
// is out of reach, so a 4K60 request on an encoder that lists 30 does not pay a refused configure.
//
// The rules:
// - The request is clamped to the rates any export offers, 24..60.
// - Rung 0 is the request at the full bit rate.
// - Rung 1 is the fallback: half the rate above 30 fps (50 -> 25, 60 -> 30, so the retry stays in the
//   request's cadence family), the rate itself at 30 and below, at 2/3 of the bit rate. One halving
//   always lands at or below 30, the floor above which a rate is halved, and never below the 24 an
//   export offers: a request of 31..47 retries at 24 (a bare `requested / 2` would give 15..23).
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

/** The rate a refused request retries at: half above the floor (never under 24), the request itself at or below it. */
constexpr int fallbackFps(int fps) { return fps > kLadderFloorFps ? std::max(fps / 2, kLadderMinFps) : fps; }

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

/** The most codecs encoderAttempts reads: the requested one, then H.264. */
inline constexpr int kAttemptMaxCodecs = 2;
/** The most attempts encoderAttempts writes. */
inline constexpr int kAttemptMaxCount = kAttemptMaxCodecs * kLadderMaxRungs;

struct EncoderAttempt {
    int codec = 0;  // index into the caller's codec list
    EncoderRung rung;
};

/**
 * Writes every attempt into out[0 .. kAttemptMaxCount) in the order to try: codec 0's ladder, then codec 1's,
 * each under its own ceiling (ceilings[i], <= 0 when unknown). Reads at most kAttemptMaxCodecs codecs and
 * returns how many attempts it wrote.
 */
constexpr int encoderAttempts(int requestedFps, int bitRate, const int* ceilings, int codecCount, EncoderAttempt* out) {
    int written = 0;
    for (int c = 0; c < std::min(codecCount, kAttemptMaxCodecs); ++c) {
        EncoderRung rungs[kLadderMaxRungs];
        const int count = encoderLadder(requestedFps, bitRate, ceilings[c], rungs);
        for (int i = 0; i < count; ++i) out[written++] = {c, rungs[i]};
    }
    return written;
}

}  // namespace geode::output
