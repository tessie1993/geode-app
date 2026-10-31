#pragma once
#include <array>
#include <string>
#include <vector>

// The scene and style catalogue: every style id the engine can draw, which renderer (kind)
// builds it, the style families, which Customize parameters reach which styles (scopes), and the
// few ranges the shell's controls quote. Pure: no GL, no JNI, so the host tests build it.
//
// The shell reads all of it through one call, text(): a versioned, line-based table that Kotlin
// (render/scene/Catalogue.kt) parses once. Display labels are not here; they stay in Kotlin, keyed
// by id, so Android can translate them. Neither is the Pro rule: the catalogue says which style
// opens a family, pro/FreeTier decides what a free user gets.
namespace geode::viz::catalogue {

// Bumped whenever a row type or field changes meaning; Kotlin refuses a table it cannot read.
constexpr int kTextVersion = 1;

// Which scene class an id builds. One per renderer, not per style. Names match Kotlin's SceneKind.
enum class Kind { Shader, Silk, Life, Mycelium, Acid, Milkdrop, Fluid, CurlFlow, Water, Cymatics };
constexpr int kKindCount = 10;

// Which styles a Customize parameter can move (R4: no dead controls; a control that cannot move
// the style on screen is hidden). Names and order match Kotlin's ParamScope.
enum class Scope {
    Universal,      // applied to the finished frame by the composite pass, so every style
    SceneClock,     // MilkDrop presets pace themselves
    AudioDrive,     // MilkDrop takes raw PCM
    BassBand,
    MidBand,
    TrebleBand,
    BandGains,      // reads at least one band envelope
    ShaderLook,     // morph, dual palette, colour map, duotone: the fragment programs
    MarchDetail,    // the raymarch step budget; per STYLE, only the marched ones
    EndlessZoom,    // integrated by the shader scenes and MilkDrop
    Turbulence,     // an in-scene force
    DyeDensity,
    TrailToggle,
    TrailLength,
    TrailEcho,      // TrailPass zoom/warp
    ParticleSprite,
    Milkdrop,
    FluidSim,
    Emitters,       // Fluid and Water splats
    Journey,        // spawn/catch choreography: Fluid, Curl Flow, Water
    Water,
    Cymatics,
    FlowFieldSim,   // Fluid drives its field from its own solver instead
    RippleOverlay,  // Water's own surface already refracts
};
constexpr int kScopeCount = 24;

struct Range {
    float min;
    float max;
};

// Twelve fragment styles on one shared GLSL library (GLOSSARY "Style family"). ids[0] opens the
// family: the first and most basic style.
struct Family {
    const char* name;
    const char* prefix;
    const char* library;  // on core/viz/ShaderSource.cpp's include allow-list
    std::array<const char*, 12> ids;
    const char* opener() const { return ids[0]; }
};

// The user's Detail control; ShaderScene turns it into the march step budget.
constexpr Range kMarchDetail{0.25f, 1.5f};
// A pinch scales Zoom and a twist turns Rotation, held to these.
constexpr Range kTouchZoom{0.3f, 3.0f};
constexpr Range kTouchRotation{-3.0f, 3.0f};
constexpr float kTouchRotationPerDegree = 0.012f;

// Every id, in the order the picker lists them: silk, life, mycelium, acid, shaders (loose, then
// the families in family order), milkdrop, fluid, curl flow, water, cymatics.
const std::vector<std::string>& allIds();
bool knows(const std::string& id);
// Unknown ids are Silk: the default style is a Silk one and the registry falls back to it, so the
// Customize panel describes what will actually be on screen.
Kind kindOf(const std::string& id);
bool isShaderStyle(const std::string& id);
// Spends the Detail budget (MarchDetail reaches it).
bool isMarched(const std::string& id);
// How the style turns by default: a signed multiplier on the motion field's drift spin, 0 for a
// style that holds still (bars, grids, landscapes and trees read as broken when they roll).
float spinRate(const std::string& id);
const std::array<Family, 9>& families();

// The scope of a Customize control, named by its ParamKeys string. Anything unlisted is Universal.
Scope scopeOf(const std::string& paramKey);
bool reaches(Scope scope, const std::string& styleId);

const char* name(Kind kind);
const char* name(Scope scope);

// The whole catalogue as the shell reads it. Built once; the reference lives for the process.
// Rows, '|'-separated, one per line, in this order:
//   catalogue|<kTextVersion>
//   kind|<KIND>|<SCOPE>,<SCOPE>,...       every scope that reaches the kind (MarchDetail never: it is per style)
//   style|<id>|<KIND>[|MARCH_DETAIL]       allIds() order; the extra field lists per-style scopes
//   family|<name>|<prefix>|<library>|<id>,<id>,...   opener first
//   key|<ParamKeys string>|<SCOPE>         every non-Universal control
//   range|<ParamKeys string>|<min>|<max>
//   value|<name>|<number>
const std::string& text();

}  // namespace geode::viz::catalogue
