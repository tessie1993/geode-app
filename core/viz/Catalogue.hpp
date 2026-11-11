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
// 2: the style row carries its look group.
constexpr int kTextVersion = 2;

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

// The Styles page's group a style is shown in: what it looks like, not which renderer draws it
// (the owner's groups, 2026-11-02). Names and order match Kotlin's model/StyleLook, which keeps the
// labels people read. Per-style metadata is engine data (ADR 0003), so the tags live here beside the
// kind and family they mostly follow, and a style the engine moves between families moves group too.
enum class Look { Flowing, Living, Psychedelic, Fractals, Space, FlyThrough, Glass, Patterns, Classic };
constexpr int kLookCount = 9;

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
    Look look;            // every style of the family is shown in this group
    std::array<const char*, 12> ids;
    const char* opener() const { return ids[0]; }
};

// Morph World: the one style whose ShaderScene runs a World Genome driver (core/viz/world).
constexpr const char* kMorphWorld = "morph_world";

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
// Its shader reads the camera rig (uCamBasis), so ShaderScene::drawsCamera() will be true and the
// Camera framing and feel controls move it. style_reads_test pins both facts to the shaders.
bool fliesCamera(const std::string& id);
// How the style turns by default: a signed multiplier on the motion field's drift spin, 0 for a
// style that holds still (bars, grids, landscapes and trees read as broken when they roll).
float spinRate(const std::string& id);
const std::array<Family, 10>& families();
// The group a style is shown in: a family style by its family, a dedicated renderer's style by its
// kind, a loose shader style by its own tag. Unknown ids are Silk's, so Flowing.
Look lookOf(const std::string& id);

// The scope of a Customize control, named by its ParamKeys string. Anything unlisted is Universal.
Scope scopeOf(const std::string& paramKey);
bool reaches(Scope scope, const std::string& styleId);

const char* name(Kind kind);
const char* name(Scope scope);
const char* name(Look look);

// The whole catalogue as the shell reads it. Built once; the reference lives for the process.
// Rows, '|'-separated, one per line, in this order:
//   catalogue|<kTextVersion>
//   kind|<KIND>|<SCOPE>,<SCOPE>,...       every scope that reaches the kind (MarchDetail never: it is per style)
//   style|<id>|<KIND>|<LOOK>[|MARCH_DETAIL]   allIds() order; the extra field lists per-style scopes
//   family|<name>|<prefix>|<library>|<id>,<id>,...   opener first
//   override|<id>|<ParamKeys string>|<value>   the style holds that control at that value
//   key|<ParamKeys string>|<SCOPE>         every non-Universal control
//   range|<ParamKeys string>|<min>|<max>
//   value|<name>|<number>
const std::string& text();

}  // namespace geode::viz::catalogue
