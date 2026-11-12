// Host-side checks for core/viz/TransitionLibrary (#438): the GL Transitions library parse the renderer
// uses, and the picker table the shell reads through geode_transition_picker. Built and run by CI
// ("Native host tests" in .github/workflows/native-host-tests.yml) with the system compiler; no GL.
//
// What is pinned: the parse rules (missing default → one 0, booleans → 0/1, arrays kept, non-objects
// skipped), and that the picker table splits back into the built-ins offered and every library name in
// library order, here and for the shipped asset (app/src/main/assets/gl_transitions.json, read from the
// repo root as CI runs it).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "viz/TransitionChoice.hpp"
#include "viz/TransitionLibrary.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t at = s.find(sep, start);
        if (at == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, at - start));
        start = at + 1;
    }
}

// The split the shell does on the picker table (ui/CustomizeAdvanced): two lists on RS, entries on US,
// empty entries dropped.
struct Picker {
    std::vector<std::string> builtIns;
    std::vector<std::string> library;
};

Picker decode(const std::string& table) {
    const auto lists = split(table, kTableList);
    const auto entries = [&](size_t i) {
        std::vector<std::string> out;
        if (i >= lists.size()) return out;
        for (std::string& entry : split(lists[i], kTableEntry)) {
            if (!entry.empty()) out.push_back(std::move(entry));
        }
        return out;
    };
    return Picker{entries(0), entries(1)};
}

std::vector<std::string> namesOf(const std::vector<TransitionDef>& defs) {
    std::vector<std::string> names;
    for (const auto& d : defs) names.push_back(d.name);
    return names;
}

const std::vector<std::string> kBuiltIns = {"cut", "fade", "melt", "slide", "zoom"};

void parseRules() {
    const std::string json = R"([
      {"name": "fade", "author": "A", "license": "MIT", "glsl": "vec4 transition(vec2 uv) {\n\treturn vec4(1);\n}",
       "paramsTypes": {"amount": "float", "flip": "bool", "dir": "vec2", "missing": "float", "odd": 3},
       "defaultParams": {"amount": 0.25, "flip": true, "dir": [1, -0.5], "odd": 2}},
      42,
      {"name": "bare", "glsl": "x"}
    ])";
    const auto defs = parseTransitionLibrary(json);
    check(defs.size() == 2, "a non-object entry is skipped");
    if (defs.size() != 2) return;
    const TransitionDef& fade = defs[0];
    check(fade.name == "fade" && fade.author == "A" && fade.license == "MIT", "name, author, license");
    auto param = [&](const std::string& name) -> const TransitionParam* {
        for (const auto& p : fade.params) {
            if (p.name == name) return &p;
        }
        return nullptr;
    };
    check(param("amount") && param("amount")->values == std::vector<float>{0.25f}, "a number default");
    check(param("flip") && param("flip")->values == std::vector<float>{1.0f} && param("flip")->type == "bool", "a boolean default reads 1");
    check(param("dir") && param("dir")->values == (std::vector<float>{1.0f, -0.5f}), "an array default keeps every value");
    check(param("missing") && param("missing")->values == std::vector<float>{0.0f}, "a missing default reads one 0");
    check(param("odd") && param("odd")->type == "float", "a type that is not a string reads float");
    check(defs[1].name == "bare" && defs[1].params.empty() && defs[1].author.empty(), "an entry without params");
    check(parseTransitionLibrary("{}").empty() && parseTransitionLibrary("not json").empty(), "text that is not an array parses empty");
}

// The picker lists what the shell offers: the built-ins without "cut" and with "random", then the library.
void pickerListsBuiltInsThenTheLibrary() {
    const auto builtIns = transition_choice::pickerIds(kBuiltIns);
    check(builtIns == (std::vector<std::string>{"fade", "melt", "slide", "zoom", "random"}),
          "\"cut\" is not offered (safety plays it as a fade) and \"random\" closes the list");
    const auto defs = parseTransitionLibrary(R"([{"name": "doom", "glsl": "x"}, {"name": "ripple", "glsl": "y"}])");
    const Picker picker = decode(encodeTransitionPicker(builtIns, defs));
    check(picker.builtIns == builtIns, "the built-ins split back");
    check(picker.library == (std::vector<std::string>{"doom", "ripple"}), "the library's names, in library order");
    const Picker bare = decode(encodeTransitionPicker(builtIns, {}));
    check(bare.builtIns == builtIns && bare.library.empty(), "no library is the built-ins alone");
}

// JSON can spell the table's separators (\u001e, \u001f) in a name; the encoder writes them as spaces, so the
// table still splits into one entry per transition.
void separatorsInNames() {
    const std::string json = R"([
      {"name": "a\u001eb", "glsl": "x"},
      {"name": "c\u001fd", "glsl": "y"},
      {"name": "next", "glsl": "z"}
    ])";
    const auto defs = parseTransitionLibrary(json);
    check(defs.size() == 3 && defs[0].name == "a\x1e" "b", "the JSON escapes reach the parse");
    const Picker picker = decode(encodeTransitionPicker({"fade"}, defs));
    check(picker.library == (std::vector<std::string>{"a b", "c d", "next"}), "a separator in a name reads as a space");
    check(picker.builtIns == std::vector<std::string>{"fade"}, "and never spills into the built-ins");
}

void shippedAsset() {
    std::ifstream in("app/src/main/assets/gl_transitions.json", std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "FAIL: run from the repo root: app/src/main/assets/gl_transitions.json not found\n");
        failures++;
        return;
    }
    std::stringstream text;
    text << in.rdbuf();
    const auto defs = parseTransitionLibrary(text.str());
    check(defs.size() >= 100, "the shipped library parses (122 transitions as of #438)");
    const Picker picker = decode(encodeTransitionPicker(transition_choice::pickerIds(kBuiltIns), defs));
    check(picker.library == namesOf(defs), "every shipped name reaches the picker, in order");
    bool everyGlsl = true;
    for (const auto& d : defs) everyGlsl = everyGlsl && !d.name.empty() && d.glsl.find("transition") != std::string::npos;
    check(everyGlsl, "every shipped transition has a name and a transition() body");
}

}  // namespace

int main() {
    parseRules();
    pickerListsBuiltInsThenTheLibrary();
    separatorsInNames();
    shippedAsset();
    if (failures == 0) std::printf("transition_library_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
