// Host-side checks for core/viz/TransitionLibrary (#438): the GL Transitions library parse the renderer
// and the shell share, and the table the shell reads through geode_transitions_table. Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the parse rules the Kotlin parser had (missing default → one 0, booleans → 0/1, arrays
// kept, non-objects skipped), and that the table splits back into exactly what was parsed, here and for
// the shipped asset (app/src/main/assets/gl_transitions.json, read from the repo root as CI runs it).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

std::vector<std::string> split(const std::string& s, char sep, size_t limit = 0) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        if (limit != 0 && out.size() + 1 == limit) {
            out.push_back(s.substr(start));
            return out;
        }
        const size_t at = s.find(sep, start);
        if (at == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, at - start));
        start = at + 1;
    }
}

// The decode render/TransitionCatalog.kt parseTable does, in C++, so a round trip proves the shell sees
// what the renderer parsed.
std::vector<TransitionDef> decode(const std::string& table) {
    std::vector<TransitionDef> defs;
    if (table.empty()) return defs;
    for (const std::string& d : split(table, kTableDef)) {
        const auto f = split(d, kTableField, 5);
        TransitionDef def;
        if (f.size() != 5) return {};
        def.name = f[0];
        def.author = f[1];
        def.license = f[2];
        def.glsl = f[4];
        if (!f[3].empty()) {
            for (const std::string& p : split(f[3], kTableParam)) {
                const auto pf = split(p, kTableParamField);
                if (pf.size() != 3) return {};
                TransitionParam param{pf[0], pf[1], {}};
                if (!pf[2].empty()) {
                    for (const std::string& v : split(pf[2], ',')) param.values.push_back(std::strtof(v.c_str(), nullptr));
                }
                def.params.push_back(std::move(param));
            }
        }
        defs.push_back(std::move(def));
    }
    return defs;
}

bool same(const std::vector<TransitionDef>& a, const std::vector<TransitionDef>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].author != b[i].author || a[i].license != b[i].license || a[i].glsl != b[i].glsl) return false;
        if (a[i].params.size() != b[i].params.size()) return false;
        for (size_t p = 0; p < a[i].params.size(); ++p) {
            const auto& x = a[i].params[p];
            const auto& y = b[i].params[p];
            if (x.name != y.name || x.type != y.type || x.values != y.values) return false;
        }
    }
    return true;
}

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
    check(same(decode(encodeTransitionTable(defs)), defs), "the table round-trips, tabs and newlines in the GLSL included");
    check(encodeTransitionTable({}).empty() && decode("").empty(), "an empty library is an empty table");
}

// JSON can spell the table's separators (\u001c..\u001f) in any field; the encoder writes them as spaces,
// so the table still splits into the same defs and params the shell's parseTable expects.
void separatorsInFields() {
    const std::string json = R"([
      {"name": "a\u001eb", "author": "c\u001fd", "license": "e\u001df", "glsl": "// g\u001ch\nvec4 transition(vec2 uv) { return vec4(1); }",
       "paramsTypes": {"p\u001cq": "fl\u001doat", "r": "float"}, "defaultParams": {"p\u001cq": 1, "r": 2}},
      {"name": "next", "glsl": "x"}
    ])";
    const auto defs = parseTransitionLibrary(json);
    check(defs.size() == 2 && defs[0].name == "a\x1e" "b", "the JSON escapes reach the parse");
    const auto decoded = decode(encodeTransitionTable(defs));
    check(decoded.size() == 2, "a separator in a field does not split the table");
    if (decoded.size() != 2) return;
    const TransitionDef& d = decoded[0];
    check(d.name == "a b" && d.author == "c d" && d.license == "e f", "separators in name, author, license read as spaces");
    check(d.glsl.find("// g h\n") == 0, "a separator in the GLSL reads as a space");
    check(d.params.size() == 2 && d.params[0].type == "fl oat" && d.params[0].values == std::vector<float>{1.0f}, "a separator in a param reads as a space");
    check(decoded[1].name == "next", "the next def is intact");
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
    check(same(decode(encodeTransitionTable(defs)), defs), "the shipped library round-trips through the shell's table");
    bool everyGlsl = true;
    for (const auto& d : defs) everyGlsl = everyGlsl && !d.name.empty() && d.glsl.find("transition") != std::string::npos;
    check(everyGlsl, "every shipped transition has a name and a transition() body");
}

}  // namespace

int main() {
    parseRules();
    separatorsInFields();
    shippedAsset();
    if (failures == 0) std::printf("transition_library_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
