#pragma once

#include <string>
#include <string_view>

#include "scene/generator/Assignment.hpp"

// The assignment as the shell reads it (geode_viz_generator_text, #472), the way it reads the catalogue
// (geode_catalogue_text): one line per slot in Slot order, then the policy, '\t'-separated:
//
//   0\t<id>\t<pinned 0|1>     slot 0 (Sky); <id> is a pool style id, empty for no generator
//   1\t<id>\t<pinned>         Wall
//   2\t<id>\t<pinned>         Material
//   3\t<id>\t<pinned>         CameraPlane
//   policy\t<n>               Policy (0 OnlyMe, 1 DirectorSwaps)
//
// Every line ends in '\n'. The slot is its number (the setters' slot argument), not its name, so the
// shell needs no second table. Style ids are stable ASCII and never hold a tab or a newline.
//
// Pure: host-tested in core/tests/generator_test.cpp; the parse is the round trip's other half.

namespace geode::scene::generator {

std::string assignmentText(const Assignment& assignment);

// Reads assignmentText's format back. Strict: every slot exactly once, the policy once, nothing else
// (blank lines and a trailing '\r' per line are allowed). On failure `out` is untouched and the result
// is false.
bool parseAssignmentText(std::string_view text, Assignment& out);

}  // namespace geode::scene::generator
