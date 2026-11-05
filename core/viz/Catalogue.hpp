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

// Note & key reactions' four bloom looks (viz/NoteResponse, lib_bloom), in SceneParams::bloomLook's
// numbering (0 there means "the style's own", this table's pick). Names match Kotlin's BloomLook.
enum class BloomLook { Flower = 1, Glow = 2, Ripple = 3, Burst = 4 };
constexpr int kBloomLookCount = 4;

// How the Scene camera (core/scene/camera, #512) may move in a style (#473). A marched style flies
// the free camera: Rails clamps it to today's rig limits (forward flight, yaw ±0.6, pitch ±0.45,
// roll ±0.8 rad, no flips) until its family is ported to a full environment, SixDof leaves it free.
// None is every style that does not march (2D shaders, the simulations, MilkDrop): it has no Scene
// camera, so the renderer leaves it as it is today.
enum class CameraMode { None, Rails, SixDof };

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
const std::array<Family, 10>& families();
// The note bloom a style draws when the user leaves Customize's bloom look on Auto: the fractal flower
// for the neon, psychedelic and petal styles, a soft glow for the gas, glass and light ones, a ripple
// ring for water, waves and landscapes, a particle burst for fly-throughs, rain and particle fields.
BloomLook bloomLook(const std::string& id);
// SceneParams::bloomLook resolved for a style: the user's pick, or the style's own on Auto (0).
BloomLook resolveBloomLook(int userPick, const std::string& id);

// The camera mode of a style: None unless it marches, then Rails until its port lists it as 6-DoF.
CameraMode cameraMode(const std::string& id);

// The scope of a Customize control, named by its ParamKeys string. Anything unlisted is Universal.
Scope scopeOf(const std::string& paramKey);
bool reaches(Scope scope, const std::string& styleId);

const char* name(Kind kind);
const char* name(Scope scope);
const char* name(BloomLook look);

// The whole catalogue as the shell reads it. Built once; the reference lives for the process.
// Rows, '|'-separated, one per line, in this order:
//   catalogue|<kTextVersion>
//   kind|<KIND>|<SCOPE>,<SCOPE>,...       every scope that reaches the kind (MarchDetail never: it is per style)
//   style|<id>|<KIND>[|MARCH_DETAIL]       allIds() order; the extra field lists per-style scopes
//   family|<name>|<prefix>|<library>|<id>,<id>,...   opener first
//   override|<id>|<ParamKeys string>|<value>   the style holds that control at that value
//   key|<ParamKeys string>|<SCOPE>         every non-Universal control
//   range|<ParamKeys string>|<min>|<max>
//   value|<name>|<number>
//   bloom|<LOOK>|<id>,<id>,...             each style's own note bloom; every style in exactly one row
//   rails|<id>,<id>,...                    every style on CameraMode::Rails, allIds() order. A row type of
//                                          its own (not a style| field) so an older shell skips it
const std::string& text();

}  // namespace geode::viz::catalogue
