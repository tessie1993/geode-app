#pragma once
#include <array>
#include <string_view>

#include "viz/Reactions.hpp"

namespace geode::viz {

// Style DNA: the reaction routes a style plays until the user edits them,
// built from the style id and a seed.
//
// Before this, every style got the same KINDS of route (kick -> camera push,
// a band -> zoom, a hit -> twist or morph), so every style zoomed in on the
// kick and twisted, with only the amounts differing. Now each style draws a
// personality: what its picture does on the beat. A Chroma style changes hue
// on every beat, a Flash style blooms, a Swing style looks somewhere new, a
// Warp style bends. It also draws a second personality for alternate phrases
// (PhraseBeatA / PhraseBeatB), so the answer to the beat changes every eight
// bars or at a section change, plus layers for the backbeat, the sustained
// bands, the top end, colour over time, build-ups and drops, each drawn so it
// does not repeat what the beat already does.
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
    // The DNA leaves the last slots free, so the user and the Events tab's
    // recipes can add routes without removing any.
    static constexpr int kMaxRoutes = ReactionEngine::kRoutes - 3;

    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    int count = 0;
    Personality lead = Personality::Pulse;    // the beat in phrase A
    Personality second = Personality::Chroma; // the beat in phrase B
};

StyleDna buildStyleDna(std::string_view styleId, int seed, const StyleTraits& traits);

}  // namespace geode::viz
