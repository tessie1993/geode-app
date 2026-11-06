#pragma once

#include <string>
#include <vector>

// Which styles can be generators (#471): every catalogue style whose kind is a 2D shader style or a
// simulation, in catalogue order (core/viz/Catalogue, allIds()). A generator is named by its style id.
//
// "2D" is what the catalogue can say: a shader style that does not march (no MARCH_DETAIL). A marched
// style is a 3D world of its own, already filmed by the camera, so it would be a scene, not a texture
// on one. The simulations are the field scenes that present a 2D grid: Silk, Fluid, Life, Acid and
// Cymatics. MilkDrop (projectM owns its own frame), Mycelium, Curl Flow and Water are not generators
// yet: each is a one-off renderer whose offscreen behaviour is unproven.
//
// Pure: no GL, so host tests read it.

namespace geode::scene::generator {

// From the native catalogue directly.
std::vector<std::string> generatorPool();

// From catalogue text (catalogue::text()'s format: "style|<id>|<KIND>[|MARCH_DETAIL]" rows; every
// other row is ignored), so whatever reads the shell's table gets the same list. Null or empty: none.
std::vector<std::string> generatorPool(const char* catalogueText);

}  // namespace geode::scene::generator
