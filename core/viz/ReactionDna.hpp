#pragma once
#include <array>
#include <string_view>

#include "viz/Reactions.hpp"

namespace geode::viz {

// Style DNA: the reaction routes a style plays until the user edits them,
// built from the style id and a seed.
//
// Each style draws a personality: what its picture does on the pulse. A Chroma
// style changes hue, a Flash style blooms, a Swing style looks somewhere new, a
// Warp style bends. It draws a second one from another group for the snare
// side of the groove, then layers for the big moments, the sustained levels,
// the top end, colour over the song, rises and the melody. No layer has a
// fixed source: each draws its own from a pool of GeodeSoundFrame sources
// (which kick, which level, what drives hue), so styles that share a
// personality still listen to different parts of the music.
//
// Pure and deterministic: the same id, seed and traits always give the same
// routes, on every platform, so the live view, the wallpaper, the dream and an
// export all react alike. Kotlin only asks for it (geode_style_reactions).

enum class Personality {
    Pulse,   // the camera pushes in on the beat (the old behaviour, now one of many)
    Chroma,  // the hue lands somewhere new on each beat
    Flash,   // brightness and bloom swell on the beat (soft: luminance floors and safety apply)
    Twist,   // a radial twist, a new direction per beat
    Warp,    // the domain bends on the beat
    Ripple,  // rings run out on the beat
    Glitch,  // chromatic split and glitch on the beat
    Swing,   // the camera looks somewhere new per beat
    Lens,    // a fisheye bulge on the beat
    Spin,    // turns a notch per beat (only styles that turn)
    Morph,   // the shape morphs on the beat (only shader styles)
};
constexpr int kPersonalityCount = static_cast<int>(Personality::Morph) + 1;

// What the DNA needs to know about a style, from SceneRegistry.
struct StyleTraits {
    // SceneRegistry::spinRate: 0 for a style that holds still. Rotation and
    // camera roll routes go only to styles that turn.
    float spinRate = 0.0f;
    // A shader style reads morph and the second palette (ParamScope.SHADER_LOOK).
    bool shaderLook = false;
};

struct StyleDna {
    // The DNA uses at most half the slots, so the user, the grid and the Events tab's
    // recipes can add routes without removing any.
    static constexpr int kMaxRoutes = ReactionEngine::kRoutes / 2;

    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    int count = 0;
    Personality lead = Personality::Pulse;    // on the kick side
    Personality second = Personality::Chroma; // on the snare side
};

StyleDna buildStyleDna(std::string_view styleId, int seed, const StyleTraits& traits);

}  // namespace geode::viz
