#pragma once
#include <string>
#include <string_view>

// The saved form of the user's reaction routes (LfoStore's key): the version line "reactions/2", then
// one route per line, `enabled|SOURCE|TARGET|MODE|amount|CURVE|attackSeconds|releaseSeconds`.
//
// Reading is forgiving, as a store read must be:
// - a missing or unknown version line is no list at all (the style's DNA plays): "reactions/1" named
//   the old analyser's sources, some under names the new set reuses with another meaning;
// - a line with the wrong field count, or naming a source or target this build does not know (or the
//   target NONE), drops that route;
// - an unknown mode or curve, a flag that is not true/false, or a number that does not parse or is
//   not finite takes its default; numbers are clamped to what the reaction matrix accepts.
// Routes cross as ReactionEngine::kConfigFloats floats each, the layout of geode_viz_set_user_reactions.
namespace geode::viz::reactiontext {

constexpr float kMinAmount = -2.0f;
constexpr float kMaxAmount = 2.0f;
constexpr float kMaxAttackSeconds = 5.0f;
constexpr float kMaxReleaseSeconds = 10.0f;

// At most ReactionEngine::kRoutes routes; one whose source, target or mode is outside its enum is left out.
std::string encode(const float* routes, int routeCount);

// Fills `out` (room for ReactionEngine::kRoutes routes) and returns how many it read; -1 for text that is no list.
int decode(std::string_view text, float* out);

}  // namespace geode::viz::reactiontext
