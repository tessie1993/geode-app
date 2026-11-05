#pragma once

#include <bit>
#include <cstdint>

// The stereo the sound engine hears, from whatever layout the source has (#362).
//
// The ring keeps every channel a source writes, with its layout, because the layout is a fact about the
// audio and the reader may want it later. But the engine hears two channels (its spectrum, its filter bank,
// its stereo width), and the owner chose no per-channel fields in the sound frame. So the ingest owns one
// downmix, and every reader goes through it: the same 5.1 file sounds the same to the screen, the wallpaper
// and an export.
//
// The weights are ITU-R BS.775's: a front channel goes to its own side at unity, the centre and every
// surround at -3 dB (1/sqrt 2), a rear or top centre at -6 dB to both sides (-3 dB into the surround pair,
// then -3 dB again into the front pair), and the LFE at -10 dB to both sides. BS.775 leaves the LFE out of
// a downmix; we keep it in at a modest level because the visuals' kicks live there in film mixes and
// electronic music mastered for 5.1. Nothing is normalised: a stereo source passes through bit-exact (the
// golden sound frames depend on it), and a loud 5.1 mix may exceed 1, which the engine's levels tolerate.
//
// Mono is the exception to the table: one channel goes to both sides at unity, so a mono file reads
// centred and narrow instead of hard left (Android calls mono FRONT_LEFT).
namespace geode::audio::ingest::downmix {

// android.media.AudioFormat CHANNEL_OUT_* bits, in the order interleaved PCM carries them.
inline constexpr uint32_t kFrontLeft = 0x4;
inline constexpr uint32_t kFrontRight = 0x8;
inline constexpr uint32_t kFrontCenter = 0x10;
inline constexpr uint32_t kLowFrequency = 0x20;
inline constexpr uint32_t kBackLeft = 0x40;
inline constexpr uint32_t kBackRight = 0x80;
inline constexpr uint32_t kFrontLeftOfCenter = 0x100;
inline constexpr uint32_t kFrontRightOfCenter = 0x200;
inline constexpr uint32_t kBackCenter = 0x400;
inline constexpr uint32_t kSideLeft = 0x800;
inline constexpr uint32_t kSideRight = 0x1000;
inline constexpr uint32_t kTopCenter = 0x2000;
inline constexpr uint32_t kTopFrontLeft = 0x4000;
inline constexpr uint32_t kTopFrontCenter = 0x8000;
inline constexpr uint32_t kTopFrontRight = 0x10000;
inline constexpr uint32_t kTopBackLeft = 0x20000;
inline constexpr uint32_t kTopBackCenter = 0x40000;
inline constexpr uint32_t kTopBackRight = 0x80000;
inline constexpr uint32_t kTopSideLeft = 0x100000;
inline constexpr uint32_t kTopSideRight = 0x200000;
inline constexpr uint32_t kBottomFrontLeft = 0x400000;
inline constexpr uint32_t kBottomFrontCenter = 0x800000;
inline constexpr uint32_t kBottomFrontRight = 0x1000000;
inline constexpr uint32_t kLowFrequency2 = 0x2000000;

inline constexpr float kMinus3dB = 0.70710678f;
inline constexpr float kMinus6dB = 0.5f;
// "A modest level": -10 dB, enough for a kick to register, not enough to swamp a film's dialogue.
inline constexpr float kLfe = 0.31622777f;

// The layout Media3 and AudioTrack give a channel count when no mask says otherwise
// (Util.getAudioTrackChannelConfig): mono, stereo, 3.0, quad, 5.0, 5.1, 6.1, 7.1. 0 past eight.
constexpr uint32_t defaultMask(int channels) {
    switch (channels) {
        case 1: return kFrontLeft;
        case 2: return kFrontLeft | kFrontRight;
        case 3: return kFrontLeft | kFrontRight | kFrontCenter;
        case 4: return kFrontLeft | kFrontRight | kBackLeft | kBackRight;
        case 5: return kFrontLeft | kFrontRight | kFrontCenter | kBackLeft | kBackRight;
        case 6: return 0xFCu;                  // 5.1: FL FR FC LFE BL BR
        case 7: return 0xFCu | kBackCenter;    // 6.1
        case 8: return 0xFCu | kSideLeft | kSideRight;   // 7.1 (CHANNEL_OUT_7POINT1_SURROUND)
        default: return 0;
    }
}

// The mask a format really means: the one given when it names exactly `channels` speakers, else the
// default for the count. Media3 often hands the tap no mask at all.
constexpr uint32_t effectiveMask(uint32_t mask, int channels) {
    return (channels > 0 && std::popcount(mask) == channels) ? mask : defaultMask(channels);
}

// The lowest `keep` speakers of mask: the ones the first `keep` interleaved channels carry, when a source
// has more channels than the ring keeps.
constexpr uint32_t firstSpeakers(uint32_t mask, int keep) {
    uint32_t out = 0;
    for (int i = 0; i < keep && mask; ++i) {
        const uint32_t low = mask & (~mask + 1u);
        out |= low;
        mask &= ~low;
    }
    return out;
}

// The left and right weight of one speaker bit.
constexpr void speakerGains(uint32_t bit, float& left, float& right) {
    switch (bit) {
        case kFrontLeft:
        case kFrontLeftOfCenter: left = 1.0f; right = 0.0f; return;
        case kFrontRight:
        case kFrontRightOfCenter: left = 0.0f; right = 1.0f; return;
        case kFrontCenter: left = kMinus3dB; right = kMinus3dB; return;
        case kLowFrequency:
        case kLowFrequency2: left = kLfe; right = kLfe; return;
        case kBackLeft:
        case kSideLeft:
        case kTopFrontLeft:
        case kTopBackLeft:
        case kTopSideLeft:
        case kBottomFrontLeft: left = kMinus3dB; right = 0.0f; return;
        case kBackRight:
        case kSideRight:
        case kTopFrontRight:
        case kTopBackRight:
        case kTopSideRight:
        case kBottomFrontRight: left = 0.0f; right = kMinus3dB; return;
        default: left = kMinus6dB; right = kMinus6dB; return;   // centres above, behind and below; unknown bits
    }
}

// Fills left[i] and right[i] for each of `channels` interleaved channels laid out as `mask` (an effective
// mask, or 0 when the source named none and has more than eight). Mono is duplicated at unity. Without a
// mask the first two channels are taken as front left and right and the rest as centres.
inline void gains(uint32_t mask, int channels, float* left, float* right) {
    if (channels == 1) {
        left[0] = 1.0f;
        right[0] = 1.0f;
        return;
    }
    for (int c = 0; c < channels; ++c) {
        uint32_t bit = 0;
        if (mask) {
            bit = mask & (~mask + 1u);
            mask &= ~bit;
        } else {
            bit = c == 0 ? kFrontLeft : c == 1 ? kFrontRight : kFrontCenter;
        }
        speakerGains(bit, left[c], right[c]);
    }
}

}  // namespace geode::audio::ingest::downmix
