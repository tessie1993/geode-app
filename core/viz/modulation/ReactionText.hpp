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
// Routes cross as ReactionEngine::kConfigFloats floats each, the layout ReactionEngine takes.
//
// The Event Responder's rows and responses (viz/modulation/Responder) are saved in the same text, after the routes,
// as lines whose field count is never a route's eight, so a build that does not know them drops them as it drops
// any line it cannot read, and a list saved before them reads as it always did:
//   routes|auto         the user has no routes of their own (the style's play); only written beside responses
//   mute|SOURCE|TARGET  one of the style's own routes the user switched off, named by what it listens to and moves
//   gate|true           read and ignored: written by a build whose rows held back the style's routes
//   row|NAME|true       a row that is on (a row not named is off)
//   response|ROW|enabled|KIND|TARGET|amount|CURVE|fadeIn|hold|fadeOut|UNIT|DRIVE|everyNth|chance|cooldown|p0|p1|p2|p3|p4|p5
// Reading forgives as the routes do: an unknown row or target, or the wrong field count, drops the line; an unknown
// name or a number that does not parse takes its default; numbers are held inside what the bank accepts.
namespace geode::viz::responder {
struct Config;
}

namespace geode::viz::reactiontext {

constexpr float kMinAmount = -2.0f;
constexpr float kMaxAmount = 2.0f;
constexpr float kMaxAttackSeconds = 5.0f;
constexpr float kMaxReleaseSeconds = 10.0f;

// At most ReactionEngine::kRoutes routes; one whose source, target or mode is outside its enum is left out.
std::string encode(const float* routes, int routeCount);

// Fills `out` (room for ReactionEngine::kRoutes routes) and returns how many it read; -1 for text that is no list.
int decode(std::string_view text, float* out);

// Whether the text opens with this version's line: a list, however many routes it holds.
bool isList(std::string_view text);

// The line that says the list holds no routes of the user's.
constexpr std::string_view kAutoLine = "routes|auto";
// Whether the text carries it.
bool autoMarked(std::string_view text);

// The responses' lines (no version line, no trailing newline); "" for the empty, ungated config.
std::string encodeResponses(const responder::Config& config);
// Reads the rows and responses of a saved text into `config` (reset first). A text that is no list gives the empty one.
void decodeResponses(std::string_view text, responder::Config& config);

}  // namespace geode::viz::reactiontext
