#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "viz/modulation/Responder.hpp"

namespace geode::viz::responder {

// What the picture kinds (Things, Fronts, Fields: viz/things/ThingResponse, viz/fronts/FrontResponse,
// viz/fields/FieldResponse) share: where a response puts what it makes, the seeded draws that decide it, and the
// life a response's envelope adds up to. Pure and header-only.

// A response's origin (the kind-param each kind calls "origin"), by wire ordinal.
//   0 Middle   the middle of the picture
//   1 Random   a point drawn from the response's seed (the Bank's seeded generator, so live and export agree)
//   2 Pitch    the pitch heard as a height: across the middle, as high as the pitch (the middle while none is heard)
//   3 Pan      the stereo pan as a place across, halfway up (the middle while the sound is centred)
//   4..7       from an edge: Left, Right, Bottom, Top, entering there and travelling across
enum class Place : int { Middle = 0, Random = 1, Pitch = 2, Pan = 3, Left = 4, Right = 5, Bottom = 6, Top = 7 };
constexpr int kPlaceCount = 8;
constexpr int kFirstEdge = static_cast<int>(Place::Left);

struct Point {
    float x = 0.5f;
    float y = 0.5f;
};

// A kind-param read as a whole number held to 0..last (a wire float is already finite).
inline int ordinalParam(const Response& r, int index, int last) {
    return std::clamp(static_cast<int>(std::lround(r.params[static_cast<size_t>(index)])), 0, last);
}

inline Place placeOf(const Response& r, int index) { return static_cast<Place>(ordinalParam(r, index, kPlaceCount - 1)); }
inline bool isEdge(Place p) { return static_cast<int>(p) >= kFirstEdge; }
// 0..3 for Left, Right, Bottom, Top: the order of the fronts', things' and fields' own Edge enums.
inline int edgeIndex(Place p) { return static_cast<int>(p) - kFirstEdge; }

// xorshift32, 0..1: the Bank's own generator, run on a copy of the seed it hands a fire.
inline float draw(std::uint32_t& x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<float>(x >> 8) / 16777216.0f;
}

inline std::uint32_t seedOf(std::uint32_t seed) { return seed == 0u ? 1u : seed; }
// A distinct stream for a Follow response from its place in the config (the Bank's own seeding rule).
inline std::uint32_t followSeed(int instance) { return seedOf(0x9E3779B9u * static_cast<std::uint32_t>(instance + 1)); }

// The point a non-edge origin names. Pitch and pan come from the frame (Bank::step); Random draws two numbers.
inline Point pointOf(Place place, std::uint32_t& rng, float pitch, float pan) {
    switch (place) {
        case Place::Random: {
            const float x = draw(rng);
            return {x, draw(rng)};
        }
        case Place::Pitch: return {0.5f, std::clamp(pitch, 0.0f, 1.0f)};
        case Place::Pan: return {std::clamp(pan, 0.0f, 1.0f), 0.5f};
        default: return {};
    }
}

// A unit direction from a seeded angle.
inline Point directionOf(std::uint32_t& rng) {
    const float a = draw(rng) * 6.283185307f;
    return {std::cos(a), std::sin(a)};
}

// The shortest life a response's pieces are given, so a response with no envelope still makes something.
constexpr float kMinLife = 0.05f;
inline float lifeOf(float fadeIn, float hold, float fadeOut) { return std::max(fadeIn + hold + fadeOut, kMinLife); }

// A Follow response spawns at most this many pieces a second, however high the level (Things and Fronts).
constexpr float kFollowMaxPerSecond = 6.0f;

// Spawns owed to a Follow response over `dt` at `level` (0..1): the rate is the level times the cap, carried
// between frames, and one frame pays at most one so a long step never bursts.
inline bool followOwes(float& owed, float level, float dt) {
    owed += std::clamp(level, 0.0f, 1.0f) * kFollowMaxPerSecond * std::max(dt, 0.0f);
    owed = std::min(owed, 2.0f);
    if (owed < 1.0f) return false;
    owed -= 1.0f;
    return true;
}

}  // namespace geode::viz::responder
