#pragma once
#include <array>
#include <optional>

namespace geode::viz {
struct CameraPose;
}

namespace geode::viz::present {

// Where a simulation's field sits in the world when it is presented in 3D (lib_field3d), and where a
// finger on the screen lands on it. Pure: no GL, no allocation.
//
// THE FIELD'S COORDINATES. field_prep_frag writes the state into a texture whose x runs along the
// state's LONG side (s) and whose y along its short side (t), whichever way the screen is turned, so a
// surface always lays the long side along its long way round. Both wrap: every simulation state here
// is a torus (Life's kernel sees across each edge), so the terrain tiles it, the torus closes it both
// ways, and the sphere closes its longitude. A field texel is square on every surface.
//
// THE EYE is lib_camera's rig, placed per surface exactly as lib_field3d's fieldEye() does: the
// terrain flies the rig's path (camFly) at a height clear of the highest relief, an object is circled
// (camAround) from a distance that frames it on the screen's short side, seen from above by
// kObjectRise. The touch mapping below casts the same ray, so the constants are mirrored in the shader
// and named there.

// lib_field3d's uFieldGeom.x.
enum class Surface : int { Terrain = 0, Torus = 1, Sphere = 2 };

// The most the relief rises over its rest height: the lows' swell (30 % of 1.5) and Ripple (a quarter
// of the relief) on a full field. lib_field3d's FIELD_CEILING.
constexpr float kReliefCeiling = 1.75f;
// The terrain: the world length of the field's short side, the relief at depth 1, how far the eye sits
// above the highest relief, and how far the view is tipped below the path's heading (screen units).
constexpr float kTerrainShort = 6.0f;
constexpr float kTerrainRelief = 0.6f;
constexpr float kTerrainClearance = 0.6f;
constexpr float kTerrainTip = 0.3f;        // FIELD_TIP
constexpr float kTerrainBob = 0.35f;       // the share of the path's vertical wander the eye keeps
constexpr float kTerrainWander = 1.2f;     // ... of at most this much either way
constexpr float kTerrainFar = 30.0f;
// The objects: the torus ring and the sphere at radius 1, their relief at depth 1, and the eye.
constexpr float kTorusRelief = 0.2f;
constexpr float kSphereRelief = 0.28f;
constexpr float kObjectRise = 0.5f;        // FIELD_RISE, radians above the rig's horizon
constexpr float kObjectFrame = 1.15f;      // the object's outer radius times this fills the short side
constexpr float kBaseFocal = 1.6f;         // CameraRig::kBaseFocal: framing at the FOV dial's default
// lib_camera's camAround turn: FIELD_SPIN (two CAM_SPIN_UNITs) times CAM_AROUND_GAIN, radians per unit of travel.
constexpr float kObjectSpin = 2.0f * (6.2831853f / 1024.0f) * 8.0f;
// The rig's travel wraps at CameraRig::kTravelWrap: the terrain's tile along the travel divides it.
constexpr float kTravelWrap = 8192.0f;

struct Geometry {
    Surface surface = Surface::Terrain;
    // World length of one field period along s and t: the terrain's tile, an object's circumferences
    // (lib_field3d's uFieldShape.xy; the march's texture LOD reads texels per world unit from them).
    float tileS = 1.0f;
    float tileT = 1.0f;
    float relief = 0.0f;   // the world height of a full field at rest
    float top = 0.0f;      // relief times kReliefCeiling: nothing of the surface rises above it
    float eye = 0.0f;      // the terrain's eye height, or an object's eye distance from its centre
    float radius = 0.0f;   // the torus ring's or the sphere's radius; 0 on the terrain
    float tube = 0.0f;     // the torus tube's radius; 0 otherwise
};

// The surface for a field of longTexels x shortTexels seen on a screen of `aspect` (width / height),
// at the style's relief `depth` (1 is the surface's own). Non-finite or degenerate input is taken as 1.
Geometry geometryFor(Surface surface, float depth, int longTexels, int shortTexels, float aspect);

struct Ray {
    std::array<float, 3> origin{};
    std::array<float, 3> dir{0.0f, 0.0f, 1.0f};
};

// lib_field3d's fieldEye(): the ray through view() point (x, y) (y up, 1 at the top edge, x times the
// aspect), from the rig's pose.
Ray eyeRay(const Geometry& g, const CameraPose& pose, float x, float y);

// Where a ray meets the surface at half its relief, as field coordinates (s, t) in 0..1; nullopt when
// it misses (the sky, past the far end, beside an object). The shape controls (Kaleidoscope, Twist,
// Tile, Warp, Ripple) and the relief itself are left out: a finger lands where the field lies at rest.
std::optional<std::array<float, 2>> fieldPoint(const Geometry& g, const Ray& ray);

// The state uv (0..1, x along the state's width) that field coordinates (s, t) read: the long side is
// the state's height when it is taller than wide, and the drift not yet shifted into the state
// (fracX, fracY, texels) is taken off as field_prep_frag takes it.
std::array<float, 2> stateUv(float s, float t, int stateW, int stateH, float fracX, float fracY);

}  // namespace geode::viz::present
