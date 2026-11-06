#pragma once
#include <string>
#include <vector>

#include "viz/Params.hpp"

// The bundled looks (#648): the catalogue's presets section. Per-style content, so it lives beside the
// style catalogue (core/viz/Catalogue) rather than in the shell. Two kinds of row:
//   - twelve generic looks (Chill … Deep), which every loose shader style offers (not the style
//     families: each of their styles is already its own tuned look, and twelve generic looks apiece
//     would bury the presets people made), named "<style id> · <look>";
//   - hand-tuned variants, each on one style ("orb_lattice · Deep Sea", "fluid_ink · Sumi", …).
// A look's params are SceneParams{} with the look's values set by name (#346's table), so a value is
// spelled once, by the param's one name, and nothing else needs to change when a param is appended.
// Pure: no GL, no JNI, so the host tests build it (core/tests/presets_test.cpp).
namespace geode::viz::catalogue {

struct Preset {
    std::string name;     // the shell's preset name; " · " marks a bundled one (BuiltInPresets.isBuiltIn)
    std::string sceneId;  // a style the catalogue knows
    float attack;
    float decay;
    SceneParams params;
};

// Every bundled preset, in the order the Presets page lists them: style-major over the loose shader
// styles (allIds() order), the generic looks in their order, then the variants. Built once.
const std::vector<Preset>& presets();

// Bumped whenever a row type or field changes meaning; Kotlin refuses a table it cannot read.
constexpr int kPresetsTextVersion = 1;

// The presets as the shell reads them (geode_catalogue_presets_text). Built once. Rows, '|'-separated,
// one per line:
//   presets|<kPresetsTextVersion>|<look count>|<preset count>
//   look|<key>|<attack>|<decay>|<name>=<value>,<name>=<value>,...   the values a look sets, by param name
//   preset|<name>|<sceneId>|<look key>                              in presets() order
// A generic look's key is its name ("Chill"); a variant's is its preset name. Each look row comes before
// the first preset that names it. Numbers are in the shortest form that reads back to the same float;
// ints and flags are written as numbers (a flag 0 or 1), as the wire carries them.
const std::string& presetsText();

}  // namespace geode::viz::catalogue
