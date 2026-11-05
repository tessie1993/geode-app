// Host-side checks for the process-wide shader overrides (core/viz/ShaderOverrides, #364). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: one table every renderer reads (live, export, wallpaper, dream), so an edit set once is
// what each of them builds; an empty source clears; the generation moves on every change and only then,
// so a renderer re-applies overrides to its resident scenes exactly when they changed; and a snapshot is a
// copy, so a renderer working through one never sees a half-applied later edit.
#include <cstdio>
#include <string>

#include "viz/ShaderOverrides.hpp"

namespace so = geode::viz::shader_overrides;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void unknownIdIsEmpty() {
    so::clearAll();
    check(so::get("neon_chrome_sea").empty(), "an id never set draws its built-in shader");
    check(so::get("").empty(), "the empty id is empty");
}

void setReplaceClear() {
    so::clearAll();
    so::set("glow_vortex", "void main(){}");
    check(so::get("glow_vortex") == "void main(){}", "set is what get returns");
    check(so::get("black_hole").empty(), "setting one style leaves the others stock");
    so::set("glow_vortex", "void main(){ /* v2 */ }");
    check(so::get("glow_vortex") == "void main(){ /* v2 */ }", "a second set replaces the first");
    so::set("glow_vortex", "");
    check(so::get("glow_vortex").empty(), "an empty source clears");
    so::set("glow_vortex", "a");
    so::clear("glow_vortex");
    check(so::get("glow_vortex").empty(), "clear clears");
    so::set("", "ignored");
    check(so::snapshot().empty(), "an empty style id is never stored");
}

void generationMovesOnChangeOnly() {
    so::clearAll();
    const uint64_t g0 = so::generation();
    so::set("hex_hive", "x");
    const uint64_t g1 = so::generation();
    check(g1 > g0, "a set moves the generation");
    so::set("hex_hive", "x");
    check(so::generation() == g1, "setting the same source again does not");
    so::set("hex_hive", "y");
    const uint64_t g2 = so::generation();
    check(g2 > g1, "a replace moves the generation");
    so::clear("hex_hive");
    const uint64_t g3 = so::generation();
    check(g3 > g2, "a clear moves the generation");
    so::clear("hex_hive");
    check(so::generation() == g3, "clearing what is not set does not");
    so::set("never_set", "");
    check(so::generation() == g3, "an empty set of an unknown id does not");
}

void snapshotIsACopy() {
    so::clearAll();
    so::set("mandelbox_cathedral", "one");
    const auto before = so::snapshot();
    so::set("mandelbox_cathedral", "two");
    so::set("glow_vortex", "three");
    check(before.size() == 1, "a copy taken earlier gains no later entries");
    check(before.at("mandelbox_cathedral") == "one", "a copy taken earlier keeps its source");
    const std::string held = so::get("mandelbox_cathedral");
    so::clear("mandelbox_cathedral");
    check(held == "two", "a source read earlier is unchanged by a later clear");
    check(so::snapshot().size() == 1, "the live table has the later edits");
}

}  // namespace

int main() {
    unknownIdIsEmpty();
    setReplaceClear();
    generationMovesOnChangeOnly();
    snapshotIsACopy();
    if (failures) {
        std::printf("%d shader override check(s) failed\n", failures);
        return 1;
    }
    std::printf("shader_overrides_test: all checks passed\n");
    return 0;
}
