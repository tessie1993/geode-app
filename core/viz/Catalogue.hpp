#pragma once
#include <array>
#include <span>
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
// 2: the style row carries its look group. 3: picker rows.
constexpr int kTextVersion = 3;

// Which scene class an id builds. One per renderer, not per style. Names match Kotlin's SceneKind.
enum class Kind { Shader, Silk, Life, Mycelium, Acid, Milkdrop, Fluid, CurlFlow, Cymatics };
constexpr int kKindCount = 9;

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
    EndlessZoom,    // a stage over the finished picture (DivePass): every kind
    Turbulence,     // an in-scene force
    DyeDensity,
    TrailToggle,
    TrailLength,
    TrailEcho,      // TrailPass zoom/warp
    ParticleSprite,
    Milkdrop,
    FluidSim,
    Emitters,       // Fluid splats
    Journey,        // spawn/catch choreography: Fluid, Curl Flow
    Cymatics,
    FlowFieldSim,   // Fluid drives its field from its own solver instead
};
constexpr int kScopeCount = 22;

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

// Fragment styles on one shared GLSL library (GLOSSARY "Style family"). A family carries its own
// count (twelve in the first ten, ten since); ids[0] opens it: the first and most basic style.
struct Family {
    const char* name;
    const char* prefix;
    const char* library;  // on core/viz/ShaderSource.cpp's include allow-list
    Look look;            // every style of the family is shown in this group
    std::span<const char* const> ids;
    const char* opener() const { return ids[0]; }
};

// Morph World: the one style whose ShaderScene runs a World Genome driver (core/viz/world).
constexpr const char* kMorphWorld = "morph_world";
// Pool: a swimming pool seen from above; its ShaderScene runs the surface driver (core/viz/pool).
constexpr const char* kPool = "pool";

// The user's Detail control; ShaderScene turns it into the march step budget.
constexpr Range kMarchDetail{0.25f, 1.5f};
// A pinch scales Zoom and a twist turns Rotation, held to these.
constexpr Range kTouchZoom{0.3f, 3.0f};
constexpr Range kTouchRotation{-3.0f, 3.0f};
constexpr float kTouchRotationPerDegree = 0.012f;

// Every id, in the order the picker lists them: silk, life, mycelium, acid, shaders (loose, then
// the families in family order), milkdrop, fluid, curl flow, cymatics.
const std::vector<std::string>& allIds();
bool knows(const std::string& id);
// A retired id and the style that replaced it, so a look, playlist entry, take or template saved on
// it opens on the new one. Every other id, known or not, is itself.
struct Retired {
    const char* id;
    const char* now;
};
constexpr std::array<Retired, 1> kRetired = {{{"water", kPool}}};
const std::string& resolveId(const std::string& id);
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
std::span<const Family> families();
// The group a style is shown in: a family style by its family, a dedicated renderer's style by its
// kind, a loose shader style by its own tag. Unknown ids are Silk's, so Flowing.
Look lookOf(const std::string& id);

// The Styles page's families, one flat level in this order (owner, 2026-11-11: one dropdown, no
// family inside a family). A shader family is one; every loose shader style is in "shaders"; any
// other renderer's styles are one per kind, Fluid and Curl Flow together as "fluid". Every
// style is in exactly one, and their ids in this order are allIds(), the order the stage swipes.
struct PickerFamily {
    std::string name;
    std::vector<std::string> ids;
};
const std::vector<PickerFamily>& pickerFamilies();

// The scope of a Customize control, named by its ParamKeys string: the scope column of its fields'
// meta rows in viz/ParamTable.inc. A key no live row names (a panel piece, a typo) is Universal.
Scope scopeOf(const std::string& paramKey);
bool reaches(Scope scope, const std::string& styleId);

const char* name(Kind kind);
const char* name(Scope scope);
const char* name(Look look);

// The whole catalogue as the shell reads it. Built once; the reference lives for the process.
// Rows, '|'-separated, one per line, in this order:
//   catalogue|<kTextVersion>
//   kind|<KIND>|<SCOPE>,<SCOPE>,...       every scope that reaches the kind (MarchDetail never: it is per style)
//   retired|<id>|<id>                         resolveId(): a retired id, then the style that replaced it
//   style|<id>|<KIND>|<LOOK>[|MARCH_DETAIL]   allIds() order; the extra field lists per-style scopes
//   family|<name>|<prefix>|<library>|<id>,<id>,...   opener first
//   picker|<name>|<id>,<id>,...           pickerFamilies() order
//   override|<id>|<ParamKeys string>|<value>   the style holds that control at that value
//   key|<ParamKeys string>|<SCOPE>         every non-Universal control, in the wire order of its first field
//   range|<ParamKeys string>|<min>|<max>
//   value|<name>|<number>
const std::string& text();

}  // namespace geode::viz::catalogue
