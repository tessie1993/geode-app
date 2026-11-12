// A simulation presented in 3D (core/viz/present): where its field sits, where a finger lands on it,
// and what the composite leaves to a scene that draws the rig itself.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Field present host test"):
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/fakegl core/tests/field_present_test.cpp
//     core/viz/present/FieldGeometry.cpp -o field_present_test && ./field_present_test
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "viz/CameraRig.hpp"
#include "viz/CompositeGrade.hpp"
#include "viz/present/FieldGeometry.hpp"

using namespace geode::viz;
using namespace geode::viz::present;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool inUnit(const std::array<float, 2>& st) { return st[0] >= 0.0f && st[0] < 1.0f && st[1] >= 0.0f && st[1] < 1.0f; }

// A portrait phone's Life state (288 across, 640 up) and screen.
constexpr int kLong = 640;
constexpr int kShort = 288;
constexpr float kPortrait = 0.45f;

void theTerrainTilesTheFieldUnderAClearEye() {
    const Geometry g = geometryFor(Surface::Terrain, 1.0f, kLong, kShort, kPortrait);
    check(g.tileT == kTerrainShort, "the short side is kTerrainShort long");
    const float tiles = kTravelWrap / g.tileS;
    check(std::fabs(tiles - std::round(tiles)) < 1e-3f, "a whole number of tiles in the rig's travel wrap");
    const float ratio = static_cast<float>(kLong) / static_cast<float>(kShort);
    check(std::fabs(g.tileS / g.tileT - ratio) / ratio < 0.01f, "a field texel is square on the ground");
    check(g.top == g.relief * kReliefCeiling, "the top is the relief's ceiling");
    check(g.eye - kTerrainBob * kTerrainWander - g.top >= kTerrainClearance - 1e-4f, "the lowest eye clears the highest relief");
    const Geometry deep = geometryFor(Surface::Terrain, 2.0f, kLong, kShort, kPortrait);
    check(deep.relief > g.relief && deep.eye > g.eye, "a deeper relief lifts the eye with it");
}

void aFingerLandsOnTheGroundOrNowhere() {
    const Geometry g = geometryFor(Surface::Terrain, 1.0f, kLong, kShort, kPortrait);
    CameraPose pose;
    const auto centre = fieldPoint(g, eyeRay(g, pose, 0.0f, 0.0f));
    check(centre.has_value() && inUnit(*centre), "the screen's centre looks down onto the field");
    check(!fieldPoint(g, eyeRay(g, pose, 0.0f, 1.0f)).has_value(), "the top edge looks into the sky");
    const auto low = fieldPoint(g, eyeRay(g, pose, 0.0f, -1.0f));
    const auto lower = fieldPoint(g, eyeRay(g, pose, 0.0f, -0.5f));
    check(low && lower, "the lower half is ground");
    // Flying on moves the field under the same finger.
    CameraPose ahead = pose;
    ahead.pos[2] += 0.25f * g.tileS;
    const auto moved = fieldPoint(g, eyeRay(g, ahead, 0.0f, 0.0f));
    check(centre && moved && std::fabs(std::fmod((*moved)[0] - (*centre)[0] + 1.0f, 1.0f) - 0.25f) < 1e-3f,
          "a quarter tile of travel is a quarter of the field along s");
    CameraPose broken = pose;
    broken.pos[0] = std::numeric_limits<float>::quiet_NaN();
    check(!fieldPoint(g, eyeRay(g, broken, 0.0f, 0.0f)).has_value(), "a broken pose lands nowhere");
}

void anObjectIsFramedOnTheShortSide() {
    for (const Surface s : {Surface::Torus, Surface::Sphere}) {
        for (const float aspect : {kPortrait, 1.0f, 2.2f}) {
            const Geometry g = geometryFor(s, 1.0f, kLong, kShort, aspect);
            const float outer = g.radius + g.tube + g.top;
            check(g.eye > outer, "the eye is outside the object");
            const float halfAngle = outer / std::sqrt(g.eye * g.eye - outer * outer);
            check(halfAngle <= std::min(aspect, 1.0f) / kBaseFocal + 1e-5f, "the object fits the short side");
            CameraPose pose;
            bool any = false;
            for (int i = 0; i <= 20; ++i) {
                const float y = -0.5f + 0.05f * static_cast<float>(i);
                const auto hit = fieldPoint(g, eyeRay(g, pose, 0.0f, y));
                if (!hit) continue;
                any = true;
                check(inUnit(*hit), "an object's field coordinates are in 0..1");
            }
            check(any, "the middle column meets the object");
            check(!fieldPoint(g, eyeRay(g, pose, 3.0f, 0.0f)).has_value(), "far beside the object is the void");
        }
    }
    const Geometry torus = geometryFor(Surface::Torus, 1.0f, kLong, kShort, kPortrait);
    check(std::fabs(torus.tube - static_cast<float>(kShort) / static_cast<float>(kLong)) < 1e-5f &&
              std::fabs(torus.tileS / torus.tileT - static_cast<float>(kLong) / static_cast<float>(kShort)) < 1e-3f,
          "the torus's girth to its ring is the field's short side to its long");
    const Geometry square = geometryFor(Surface::Torus, 1.0f, 300, 300, 1.0f);
    check(square.tube < square.radius, "a square field still makes a ring with a hole");
}

void theFieldReadsTheStateTheLongWay() {
    const auto tall = stateUv(0.25f, 0.75f, kShort, kLong, 0.0f, 0.0f);
    check(tall[0] == 0.75f && tall[1] == 0.25f, "a tall state's long side is its height");
    const auto wide = stateUv(0.25f, 0.75f, kLong, kShort, 0.0f, 0.0f);
    check(wide[0] == 0.25f && wide[1] == 0.75f, "a wide state's long side is its width");
    const auto drifted = stateUv(0.5f, 0.5f, kLong, kShort, 64.0f, 0.0f);
    check(std::fabs(drifted[0] - 0.4f) < 1e-6f, "the drift not yet in the state is taken off");
    const auto wrapped = stateUv(0.0f, 0.0f, kLong, kShort, 64.0f, 0.0f);
    check(wrapped[0] >= 0.0f && wrapped[0] < 1.0f, "and wraps");
}

// Owner (2026-11-12): Kaleidoscope, Twist, Tile, Warp and Ripple bend a 3D-presented simulation's shape,
// so the composite must not bend its finished picture again, nor grade it twice; shader styles unchanged.
void theCompositeLeavesARigScenesPictureAlone() {
    const auto none = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
    check(grade::gateFor(SceneFamily::Fluid, true).toVec4() == none, "a simulation on the rig owns its shape and grade");
    check(grade::gateFor(SceneFamily::Fluid, false).toVec4() == (std::array<float, 4>{1.0f, 1.0f, 1.0f, 0.0f}),
          "a flat simulation is still bent, mirrored and graded by the composite");
    check(grade::gateFor(SceneFamily::Fluid).toVec4() == grade::gateFor(SceneFamily::Fluid, false).toVec4(), "flat by default");
    check(grade::gateFor(SceneFamily::Shader, true).toVec4() == none && grade::gateFor(SceneFamily::Shader, false).toVec4() == none,
          "a shader style is left alone either way");
    check(grade::gateFor(SceneFamily::Milkdrop, false).toVec4() == (std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f}),
          "MilkDrop keeps its geometry from the composite");
}

}  // namespace

int main() {
    theTerrainTilesTheFieldUnderAClearEye();
    aFingerLandsOnTheGroundOrNowhere();
    anObjectIsFramedOnTheShortSide();
    theFieldReadsTheStateTheLongWay();
    theCompositeLeavesARigScenesPictureAlone();
    if (failures == 0) std::printf("field_present_test: ok\n");
    return failures == 0 ? 0 : 1;
}
