// Host-side checks for the note a .milk preset gets when projectM swapped one of its shaders for the default
// (core/viz/MilkShaderNote). No GL, no projectM.
//
// What is pinned: no note while the preset kept its shaders; otherwise the note names the preset's path first,
// as a failed load's does, and says which shader was dropped.
#include <cstdio>
#include <string>

#include "viz/MilkShaderNote.hpp"

namespace milk = geode::viz::milk;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

}  // namespace

int main() {
    const std::string path = "/data/milk/Geiss - Spiral.milk";
    check(milk::droppedShaderNote(path, 0).empty(), "no note when the preset kept its shaders");
    check(milk::droppedShaderNote(path, 4).empty(), "an unknown bit is no note");

    const std::string warp = milk::droppedShaderNote(path, milk::kDroppedWarp);
    check(warp.rfind(path + ": ", 0) == 0, "the note starts with the preset's path, as a load error does");
    check(has(warp, "warp shader") && !has(warp, "composite"), "a dropped warp names the warp only");

    const std::string comp = milk::droppedShaderNote(path, milk::kDroppedComposite);
    check(has(comp, "composite shader") && !has(comp, "warp"), "a dropped composite names the composite only");

    const std::string both = milk::droppedShaderNote(path, milk::kDroppedWarp | milk::kDroppedComposite);
    check(has(both, "warp and composite shaders"), "both dropped names both");
    check(has(both, "default"), "the note says the default plays");

    if (failures == 0) std::printf("milk_shader_note_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
