// Shader truth: what the catalogue says about a shader style against what its shader reads.
// ShaderScene treats a program with an active uCamBasis as flying the camera rig and spends the
// Detail budget through uSteps, so the catalogue's two per-style facts must follow the source.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Style reads host tests").
//
//   style_reads_test [app/src/main/assets/]
//
// The real ShaderSource resolves the //#include lines over the repo's assets folder. A uniform
// counts as read when a function reachable from main() names it, comments stripped: the nearest
// a host test gets to the driver's active-uniform list.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/ShaderSource.hpp"

using namespace geode::viz;

struct AAssetManager {
    std::string root;
};
struct AAsset {
    std::string bytes;
};

extern "C" AAsset* AAssetManager_open(AAssetManager* manager, const char* filename, int /*mode*/) {
    if (manager == nullptr || filename == nullptr) return nullptr;
    std::ifstream in(manager->root + filename, std::ios::binary);
    if (!in) return nullptr;
    std::ostringstream bytes;
    bytes << in.rdbuf();
    return new AAsset{bytes.str()};
}
extern "C" off_t AAsset_getLength(AAsset* asset) { return static_cast<off_t>(asset->bytes.size()); }
extern "C" int AAsset_read(AAsset* asset, void* buf, size_t count) {
    const size_t n = std::min(count, asset->bytes.size());
    std::memcpy(buf, asset->bytes.data(), n);
    return static_cast<int>(n);
}
extern "C" void AAsset_close(AAsset* asset) { delete asset; }

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool isWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 2, "//") == 0) {
            while (i < s.size() && s[i] != '\n') ++i;
        } else if (s.compare(i, 2, "/*") == 0) {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            out += ' ';
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::set<std::string> wordsOf(const std::string& s) {
    std::set<std::string> out;
    for (size_t i = 0; i < s.size();) {
        if (!isWord(s[i])) {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < s.size() && isWord(s[i])) ++i;
        out.insert(s.substr(start, i - start));
    }
    return out;
}

// The name before the parameter list that closes right before `brace`; empty when the brace opens
// something else (a struct, an initializer).
std::string functionNameBefore(const std::string& s, size_t brace) {
    size_t i = brace;
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1]))) --i;
    if (i == 0 || s[i - 1] != ')') return "";
    int depth = 0;
    for (--i;; --i) {
        if (s[i] == ')') ++depth;
        if (s[i] == '(' && --depth == 0) break;
        if (i == 0) return "";
    }
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1]))) --i;
    const size_t end = i;
    while (i > 0 && isWord(s[i - 1])) --i;
    return s.substr(i, end - i);
}

// Every top-level function's words, by name; overloads share an entry.
std::map<std::string, std::set<std::string>> functionsOf(const std::string& s) {
    std::map<std::string, std::set<std::string>> out;
    int depth = 0;
    size_t start = 0;
    std::string name;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            if (depth++ == 0) {
                name = functionNameBefore(s, i);
                start = i;
            }
        } else if (s[i] == '}' && depth > 0 && --depth == 0 && !name.empty()) {
            out[name].merge(wordsOf(s.substr(start, i - start)));
        }
    }
    return out;
}

std::set<std::string> wordsReachedFromMain(const std::string& source) {
    const auto functions = functionsOf(stripComments(source));
    std::set<std::string> seen;
    std::set<std::string> words;
    std::vector<std::string> todo{"main"};
    while (!todo.empty()) {
        const std::string name = todo.back();
        todo.pop_back();
        const auto fn = functions.find(name);
        if (fn == functions.end() || !seen.insert(name).second) continue;
        for (const std::string& word : fn->second) {
            words.insert(word);
            if (functions.count(word) != 0) todo.push_back(word);
        }
    }
    return words;
}

void theReaderFollowsCallsNotDeclarations() {
    const std::string src =
        "uniform mat3 uCamBasis; // uSteps\n"
        "uniform float uSteps;\n"
        "struct Hit { float t; };\n"
        "vec3 camRay(vec2 uv) { return uCamBasis * vec3(uv, 1.0); }\n"
        "float march(vec3 p) { /* uCamBasis */ return uSteps; }\n"
        "float shade(vec3 p)\n{ return march(p); }\n"
        "void main() { float d = shade(vec3(0.0)); }\n";
    const auto words = wordsReachedFromMain(src);
    check(words.count("uSteps") == 1, "a uniform read two calls from main() is read");
    check(words.count("uCamBasis") == 0, "a uniform only an uncalled function or a comment names is not read");
}

void theCatalogueFollowsTheShaders(const std::string& assetsRoot) {
    AAssetManager assets{assetsRoot};
    const ShaderSource source(&assets);
    int shaders = 0;
    for (const std::string& id : catalogue::allIds()) {
        if (!catalogue::isShaderStyle(id)) {
            check(!catalogue::fliesCamera(id), id + ": only a shader style flies the camera rig");
            continue;
        }
        std::string error;
        const auto src = source.load(id + "_frag.glsl", &error);
        check(src.has_value(), id + ": " + error);
        if (!src) continue;
        ++shaders;
        const auto words = wordsReachedFromMain(*src);
        const bool rig = words.count("uCamBasis") != 0;
        const bool steps = words.count("uSteps") != 0;
        check(catalogue::fliesCamera(id) == rig,
              id + (rig ? ": reads uCamBasis, the catalogue says no camera rig" : ": no uCamBasis, the catalogue says it flies the rig"));
        check(catalogue::isMarched(id) == steps,
              id + (steps ? ": reads uSteps, the catalogue says not marched" : ": no uSteps, the catalogue says marched"));
    }
    check(shaders > 0, "no shader style read under " + assetsRoot);
}

// Customize › Look on a shader style is the style's own grade() and pal(): the composite leaves the shader
// family ungraded (grade::gateFor), so a style that stops reading one of these uniforms leaves its control dead.
void everyShaderStyleReadsTheLookControls(const std::string& assetsRoot) {
    static const char* const kLook[] = {"uPalBase",    "uPalRange", "uPal2Base", "uPaletteMix", "uPalLutMix",
                                        "uColorShift", "uHueRange", "uSat",      "uBright",     "uContrast",
                                        "uGamma",      "uIntensity", "uTemperature", "uBloom"};
    AAssetManager assets{assetsRoot};
    const ShaderSource source(&assets);
    for (const std::string& id : catalogue::allIds()) {
        if (!catalogue::isShaderStyle(id)) continue;
        const auto src = source.load(id + "_frag.glsl", nullptr);
        if (!src) continue;
        const auto words = wordsReachedFromMain(*src);
        for (const char* u : kLook) check(words.count(u) != 0, id + ": never reads " + u + ", so its Look control is dead");
    }
}

}  // namespace

int main(int argc, char** argv) {
    theReaderFollowsCallsNotDeclarations();
    theCatalogueFollowsTheShaders(argc > 1 ? argv[1] : "app/src/main/assets/");
    everyShaderStyleReadsTheLookControls(argc > 1 ? argv[1] : "app/src/main/assets/");
    if (failures == 0) std::printf("style_reads_test: ok\n");
    return failures == 0 ? 0 : 1;
}
