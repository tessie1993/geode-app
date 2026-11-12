// Shader truth: what the catalogue says about a shader style against what its shader reads.
// ShaderScene treats a program with an active uCamBasis as flying the camera rig and spends the
// Detail budget through uSteps, so the catalogue's two per-style facts must follow the source.
// And every Customize control a style is shown (core/viz/CustomizeMap) moves it, but for the known
// gaps in core/tests/style_param_gaps.txt, which may only shrink.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Style reads host tests").
//
//   style_reads_test [app/src/main/assets/] [--gaps core/tests/style_param_gaps.txt] [--core core/]
//   style_reads_test --gaps /dev/null --matrix    every style's unread fields, in the gaps file's format
//
// The real ShaderSource resolves the //#include lines over the repo's assets folder. A uniform
// counts as read when a function reachable from main() names it, comments and #ifdef'd-out code
// stripped, macros followed: the nearest a host test gets to the driver's active-uniform list.
// A dedicated renderer (Silk .. Cymatics, MilkDrop) counts a field as read when its scene sources
// name it or the composite applies it to that renderer's picture; one presented in 3D (core/viz/present:
// Life) draws through a style-like shader on the shared block, and is held to that shader as a style is.
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
#include "viz/CustomizeMap.hpp"
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

// The source as the compiler sees it for #ifdef / #ifndef on names the source itself #defines (a style's
// VIEW_OWNS_SHAPE drops view()'s generic morph, warp and turbulence); every other #if keeps all its branches.
// Each #define's body is filed under its name, so a word that names a macro reads what the macro reads
// (SND_BASS is uSound[0].x).
std::string preprocess(const std::string& s, std::map<std::string, std::set<std::string>>& macros) {
    std::set<std::string> defined;
    std::vector<bool> live{true};
    std::string out;
    std::istringstream lines(s);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string directive;
        std::string name;
        words >> directive >> name;
        if (directive == "#ifdef" || directive == "#ifndef") {
            const bool on = defined.count(name) != 0;
            live.push_back(live.back() && (directive == "#ifdef" ? on : !on));
        } else if (directive.rfind("#if", 0) == 0) {
            live.push_back(live.back());
        } else if (directive == "#else") {
            const bool outer = live.size() > 1 && live[live.size() - 2];
            if (live.size() > 1) live.back() = outer && !live.back();
        } else if (directive == "#endif") {
            if (live.size() > 1) live.pop_back();
        } else if (live.back() && directive == "#define") {
            const std::string macro = name.substr(0, name.find('('));
            defined.insert(macro);
            macros[macro].merge(wordsOf(line.substr(line.find(name) + name.size())));
        } else if (live.back()) {
            out += line;
        }
        out += '\n';
    }
    return out;
}

std::set<std::string> wordsReachedFromMain(const std::string& source, const std::set<std::string>& skip = {}) {
    std::map<std::string, std::set<std::string>> macros;
    auto functions = functionsOf(preprocess(stripComments(source), macros));
    for (auto& [name, words] : macros) functions[name].merge(words);
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
            if (functions.count(word) != 0 && skip.count(word) == 0) todo.push_back(word);
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
    const std::string guarded =
        "#define OWNS\n"
        "#define LEVEL uSound[0].x\n"
        "vec2 bend() {\n"
        "#ifndef OWNS\n"
        "    return vec2(uWarp);\n"
        "#else\n"
        "    return vec2(uTwist);\n"
        "#endif\n"
        "}\n"
        "void main() { float l = LEVEL; vec2 b = bend(); }\n";
    const auto read = wordsReachedFromMain(guarded);
    check(read.count("uSound") == 1, "a macro reads what its body reads");
    check(read.count("uWarp") == 0 && read.count("uTwist") == 1, "#ifndef on a name the source defines drops its branch");
}

// The present shader of a dedicated renderer that presents its simulation in 3D (core/viz/present/FieldPresent
// draws it as a ShaderScene); "" for one that draws flat.
std::string presentShader(catalogue::Kind kind) { return kind == catalogue::Kind::Life ? "life_show_frag.glsl" : ""; }

void theCatalogueFollowsTheShaders(const std::string& assetsRoot) {
    AAssetManager assets{assetsRoot};
    const ShaderSource source(&assets);
    int shaders = 0;
    for (const std::string& id : catalogue::allIds()) {
        const std::string present = presentShader(catalogue::kindOf(id));
        if (!catalogue::isShaderStyle(id) && present.empty()) {
            check(!catalogue::fliesCamera(id) && !catalogue::isMarched(id),
                  id + ": only a shader style or a 3D present flies the camera rig and marches");
            continue;
        }
        std::string error;
        const auto src = source.load(present.empty() ? id + "_frag.glsl" : present, &error);
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
    static const char* const kLook[] = {"uPalBase", "uPalRange",  "uPalLutMix", "uColorShift",  "uHueRange", "uSat", "uBright",
                                        "uContrast", "uGamma", "uIntensity", "uTemperature", "uBloom"};
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

// The top-level declarations of a function ahead of its body (prototypes), as "name(params)".
std::vector<std::string> prototypesOf(const std::string& source) {
    std::map<std::string, std::set<std::string>> macros;
    std::istringstream lines(preprocess(stripComments(source), macros));
    std::string code;
    std::string line;
    while (std::getline(lines, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] != '#') code += line;
        code += '\n';
    }
    std::vector<std::string> out;
    int depth = 0;
    std::string statement;
    for (const char c : code) {
        if (c == '{' || c == '}') {
            depth += c == '{' ? 1 : -1;
            statement.clear();
            continue;
        }
        if (depth > 0) continue;
        if (c != ';') {
            statement += c;
            continue;
        }
        const size_t open = statement.find('(');
        const size_t end = statement.find_last_not_of(" \t\n");
        // "type name(params)" closing the statement, nothing assigned: not a variable, a layout or a block.
        if (open != std::string::npos && end != std::string::npos && statement[end] == ')' &&
            statement.find('=') == std::string::npos) {
            size_t i = open;
            while (i > 0 && std::isspace(static_cast<unsigned char>(statement[i - 1]))) --i;
            const size_t nameEnd = i;
            while (i > 0 && isWord(statement[i - 1])) --i;
            const bool typed = i > 0 && std::isspace(static_cast<unsigned char>(statement[i - 1]));
            if (nameEnd > i && typed) out.push_back(statement.substr(i, end + 1 - i));
        }
        statement.clear();
    }
    return out;
}

void thePrototypeReaderFindsOnlyPrototypes() {
    const std::string src =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "uniform float uSteps; // vec2 commented(vec3 p);\n"
        "struct Hit { float t; };\n"
        "vec3 gPal[5];\n"
        "vec2 map(vec3 p);\n"
        "void field(vec3 p, out vec2 f,\n    out vec2 w);\n"
        "float f(float x) { return sin(x); }\n"
        "void main() { float d = f(uSteps); vec2 m = map(vec3(d)); }\n"
        "vec2 map(vec3 p) { return p.xy; }\n";
    const auto found = prototypesOf(src);
    check(found.size() == 2, "the reader finds the two prototypes and nothing else");
    check(!found.empty() && found[0] == "map(vec3 p)", "a one-line prototype is found by name");
}

// No style declares a function ahead of its body. GLSL ES 3.00 allows it, but until the Flame, Opal and
// Hollows libraries (lib_flame, lib_opal, lib_hollow: each called the style's map through a prototype) no
// shader the phones run had one, and exactly those 30 styles froze and crashed the owner's phone
// (2026-11-12). Each family's march now sits in lib_<family>_march, included after the style's map.
void noStyleDeclaresAFunctionAhead(const std::string& assetsRoot) {
    AAssetManager assets{assetsRoot};
    const ShaderSource source(&assets);
    for (const std::string& id : catalogue::allIds()) {
        if (!catalogue::isShaderStyle(id)) continue;
        const auto src = source.load(id + "_frag.glsl", nullptr);
        if (!src) continue;
        for (const std::string& p : prototypesOf(*src)) {
            check(false, id + ": declares " + p + " ahead of its body; define it before the first call");
        }
    }
}


// What carries each SceneParams field Customize can show on a shader style. ShaderScene uploads the field
// as a uniform and the composite leaves a shader style's geometry and grade to the style (grade::gateFor),
// so the style must read one of these or the control is dead on it.
struct Carrier {
    const char* field;
    std::vector<const char*> anyOf;
};
const std::vector<Carrier>& shaderCarriers() {
    static const std::vector<Carrier> kCarriers = {
        {"speed", {"uTime", "uFlowTime", "uSoundTimeSped"}},
        {"zoom", {"uZoom"}},
        {"rotation", {"uRotation"}},
        {"sway", {"uSway"}},
        {"driftX", {"uDriftX"}},
        {"driftY", {"uDriftY"}},
        {"turbulence", {"uTurbulence"}},
        {"warp", {"uWarp"}},
        {"ripple", {"uRipple"}},
        {"morph", {"uMorph"}},
        {"twist", {"uTwist"}},
        // Kaleidoscope's one slider: on from 2 folds (uKaleido) at its count (uSymmetry), SceneParams::kaleidoUniforms.
        {"kaleidoFolds", {"uKaleido", "uSymmetry"}},
        // Grow and layers (Customize › Shape › Grow), shown only on the family styles whose library draws them.
        {"spawn", {"uSpawn"}},
        {"fractal", {"uFractal"}},
        {"frontLayer", {"uFrontLayer"}},
        {"backLayer", {"uBackLayer"}},
        // Formula: shown only on the styles that keep formulas of their own (Catalogue.cpp's kFormulaStyles).
        {"formula", {"uFormula"}},
        // Shapes: shown only on the styles that melt the shape graph in (Catalogue.cpp's kShapeStyles, lib_shapes).
        {"shapes", {"uShapeMix"}},
        // The shading pass (lib_shade): Soft shadows on the styles with a lit surface (Catalogue.cpp's kShadowStyles),
        // Escape colour on the families whose folds feed it (kEscapeFamilies).
        {"shadow", {"uShadow"}},
        {"escapeColor", {"uEscapeColor"}},
        {"tile", {"uTile"}},
        {"pixelate", {"uPixelate"}},
        {"mirror", {"uMirrorX"}},
        {"marchDetail", {"uSteps"}},
        {"palette", {"uPalBase"}},
        {"paletteBaseOverride", {"uPalBase"}},
        {"paletteRangeOverride", {"uPalRange"}},
        {"paletteStop0", {"uPalStops"}},
        {"paletteStop1", {"uPalStops"}},
        {"paletteStop2", {"uPalStops"}},
        {"paletteStop3", {"uPalStops"}},
        {"paletteStop4", {"uPalStops"}},
        {"colorShift", {"uColorShift"}},
        {"hueRange", {"uHueRange"}},
        {"saturation", {"uSat"}},
        {"brightness", {"uBright"}},
        {"contrast", {"uContrast"}},
        {"gamma", {"uGamma"}},
        {"intensity", {"uIntensity"}},
        {"temperature", {"uTemperature"}},
        {"bloom", {"uBloom"}},
        {"posterize", {"uPosterize"}},
        {"duotone", {"uDuotone"}},
        {"solarize", {"uSolarize"}},
        {"invert", {"uInvert"}},
        // Audio drive is lib_sound's uSoundDrive (SoundUniforms::upload). The band trims scale the frame's
        // band fields (viz/BandTrim) before the style, its MotionField (uBassRel ..) and the
        // resolver's MotionField hear them: bass breathes Zoom, treble stirs Turbulence. The mids no longer
        // turn the drift (owner 2026-11-12, no style spins to the music).
        {"audioDrive", {"uSoundDrive"}},
        {"bassGain", {"SND_BASS", "SND_BASS_ATT", "SND_LOW", "SND_BASS_PRESENCE", "SND_FLUX_LOW", "uBassRel", "uZoom"}},
        {"midGain", {"SND_MID", "SND_MID_ATT", "SND_MIDDLE", "SND_MID_PRESENCE", "SND_FLUX_MID", "uMidRel"}},
        {"trebGain", {"SND_TREBLE", "SND_TREBLE_ATT", "SND_HIGH", "SND_TREBLE_PRESENCE", "SND_FLUX_HIGH", "uTrebRel", "uTurbulence"}},
        // MotionField's dials land on other fields (MotionField::apply).
        {"motionAmount", {"uWarp", "uMorph", "uTurbulence", "uTime"}},
        {"motionBreath", {"uZoom"}},
        {"motionDrift", {"uRotation"}},
        {"motionHue", {"uColorShift"}},
        // Note & key's amounts drive the style knobs (NoteResponse).
        {"notePitch", {"uKnob"}},
        {"noteIntervals", {"uKnob"}},
        {"noteTension", {"uKnob"}},
        {"noteChords", {"uKnob"}},
        {"noteDrums", {"uKnob"}},
    };
    return kCarriers;
}

// Fields a shared stage applies to every shader style's finished picture, so no style has to read them:
// the composite's lens and film (chromaAb .. fisheye), Trails and Liquid (LiquidPass), the dive (DivePass),
// the flat camera (the composite's camFlat, or the rig on a style that flies it, which
// theCatalogueFollowsTheShaders pins), and the reaction and glide settings that move other fields.
const std::set<std::string>& sharedStageFields() {
    static const std::set<std::string> kShared = {
        "chromaAb", "vignette", "scanlines", "grain", "fisheye",
        "trails", "trailLength", "trailZoom", "trailWarp",
        "flowEnabled", "flowStrength", "flowForce", "flowCurl", "liquidBorn", "liquidAge", "liquidSurface",
        "endlessZoom", "endlessZoomSpeed", "camFlat",
        // The dive's tunnel (Customize › Dive): DivePass draws it over every style kind's finished picture.
        "diveSpacing", "diveTwist", "diveAimX", "diveAimY", "diveShape", "diveBlend", "diveFog", "diveLens", "diveFollow",
        "camPath", "camFly", "camOrbit", "camFov", "camShake", "camPush", "camYaw", "camPitch", "camRoll", "camSmooth",
        "camTouch", "camGyro",
        "reactAmount", "reactSeed", "paramFadeSec",
        // Read by nothing and drawn by no control (ParamTable.inc); kept in @notes_key's Reset only.
        "noteColor",
    };
    return kShared;
}

// The known gaps, core/tests/style_param_gaps.txt: "<style>: <field> <field> ..." per line, '#' comments
// saying why. The file may only shrink: a gap missing from it fails, and so does a listed one that closed.
std::map<std::string, std::set<std::string>> readGaps(const std::string& path) {
    std::map<std::string, std::set<std::string>> out;
    std::ifstream in(path);
    check(static_cast<bool>(in), "cannot read " + path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::istringstream fields(line.substr(colon + 1));
        std::string field;
        while (fields >> field) out[line.substr(0, colon)].insert(field);
    }
    return out;
}

const Carrier* carrierOf(const std::string& field) {
    for (const auto& c : shaderCarriers()) {
        if (field == c.field) return &c;
    }
    return nullptr;
}

// Every SceneParams field Customize shows on styleId, with the control that shows it.
std::map<std::string, std::string> shownFields(const std::string& styleId) {
    std::map<std::string, std::string> out;
    for (const auto& section : customize::sections()) {
        if (!customize::shows(section, styleId)) continue;
        for (const char* control : section.controls) {
            if (!customize::reaches(control, styleId)) continue;
            for (const char* field : customize::fieldsOf(control)) out.emplace(field, control);
        }
    }
    return out;
}

// view() and arbView() hear the sound only inside Ripple's branch, so a sound read counts outside them.
const std::set<std::string> kViews = {"view", "arbView"};

bool soundName(const std::string& name) { return name.rfind("SND_", 0) == 0 || name.rfind("uSound", 0) == 0; }

// Morph World spends Morph, Warp, Turbulence, the sound (Audio drive and the band trims through the frame
// and the resolver's Turbulence) and the style knobs through its World Genome driver (ShaderScene feeds
// world::DriverInput), which reaches the shader as uWorld.
bool worldCarries(const std::string& styleId, const std::string& field) {
    static const std::set<std::string> kWorld = {"morph",    "warp",         "turbulence",    "audioDrive", "bassGain", "trebGain",
                                                 "noteDrums", "noteIntervals", "noteTension", "noteChords", "notePitch"};
    return styleId == catalogue::kMorphWorld && kWorld.count(field) != 0;
}

// Pool spends Morph (the eye's tilt), Warp (the depth) and Turbulence (the chop) on its surface through
// core/viz/pool (ShaderScene feeds pool::Controls), which reaches the shader as uPoolWaves and uPoolView.
bool poolCarries(const std::string& styleId, const std::string& field) {
    static const std::set<std::string> kPool = {"morph", "warp", "turbulence"};
    return styleId == catalogue::kPool && kPool.count(field) != 0;
}

bool readsField(const std::string& styleId, const std::string& field, const std::set<std::string>& words,
                const std::set<std::string>& heard) {
    // MotionField's spin only turns a style the catalogue gives a spin rate; no sound turns it.
    const bool spins = catalogue::spinRate(styleId) != 0.0f;
    if (field == "motionDrift") return spins && words.count("uRotation") != 0;
    if (worldCarries(styleId, field)) return words.count("uWorld") != 0;
    if (poolCarries(styleId, field)) return words.count("uPoolWaves") != 0 && words.count("uPoolView") != 0;
    const Carrier* c = carrierOf(field);
    if (c == nullptr) return false;
    for (const char* u : c->anyOf) {
        if ((soundName(u) ? heard : words).count(u) != 0) return true;
    }
    return false;
}

// The scene sources each dedicated renderer reads its SceneParams in, under core/.
std::vector<std::string> kindSources(catalogue::Kind kind) {
    using K = catalogue::Kind;
    const std::vector<std::string> fluid = {"viz/scenes/FluidSceneBase.cpp", "viz/fluid/FluidMath.cpp",     "viz/fluid/FluidEmitters.cpp",
                                            "viz/fluid/FluidChoreography.cpp", "viz/fluid/FluidLook.cpp", "viz/fluid/FluidParticles.cpp",
                                            "viz/fluid/FluidSim.cpp"};
    auto with = [&](std::vector<std::string> own) {
        own.insert(own.end(), fluid.begin(), fluid.end());
        return own;
    };
    switch (kind) {
        case K::Silk: return {"viz/scenes/SilkScene.cpp"};
        case K::Life: return {"viz/scenes/LifeScene.cpp"};
        case K::Mycelium: return {"viz/scenes/MycoScene.cpp"};
        case K::Acid: return {"viz/scenes/AcidScene.cpp"};
        case K::Milkdrop: return {"viz/scenes/MilkdropScene.cpp"};
        case K::Cymatics: return {"viz/scenes/CymaticsScene.cpp", "viz/scenes/CymaticsMath.cpp"};
        case K::Fluid: return with({"viz/scenes/FluidScene.cpp"});
        case K::CurlFlow: return with({"viz/scenes/CurlFlowScene.cpp"});
        case K::Shader: break;
    }
    return {};
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << in.rdbuf();
    return bytes.str();
}

// What the composite applies to the finished picture of a scene outside the shader family (grade::gateFor):
// the geometry and stylize group to MilkDrop and the fluid family alike, mirror, invert and the colour grade
// (zoom and rotation with it) to the fluid family only; MilkDrop's own post pass does those.
bool compositeApplies(catalogue::Kind kind, const std::string& field) {
    static const std::set<std::string> kGeo = {"warp",  "ripple",    "kaleidoFolds", "pixelate", "tile",        "twist",
                                               "bloom", "posterize", "driftX",       "driftY",   "sway",     "temperature", "solarize"};
    static const std::set<std::string> kFluidOnly = {"mirror",   "invert",     "zoom",  "rotation",   "saturation", "brightness",
                                                     "intensity", "contrast", "gamma", "colorShift"};
    if (kGeo.count(field) != 0) return true;
    return kind != catalogue::Kind::Milkdrop && kFluidOnly.count(field) != 0;
}

// The words a dedicated renderer's sources name that carry each field it is shown, where the field's own
// name is not one: the palette through SceneParams::paletteBase/Range, the band trims through the frame's
// band fields, the motion dials through the fields MotionField moves.
bool kindReads(catalogue::Kind kind, const std::string& styleId, const std::string& field, const std::set<std::string>& words) {
    static const std::map<std::string, std::vector<const char*>> kVia = {
        {"palette", {"paletteBase"}},
        {"paletteBaseOverride", {"paletteBase"}},
        {"paletteRangeOverride", {"paletteRange"}},
        {"paletteStop0", {"paletteBase", "paletteRange"}},
        {"paletteStop1", {"paletteBase", "paletteRange"}},
        {"paletteStop2", {"paletteBase", "paletteRange"}},
        {"paletteStop3", {"paletteBase", "paletteRange"}},
        {"paletteStop4", {"paletteBase", "paletteRange"}},
        {"bassGain", {"bass", "bassAtt", "low", "bassPresence", "fluxLow"}},
        {"midGain", {"mid", "midAtt", "middle", "midPresence", "fluxMid"}},
        {"trebGain", {"treble", "trebleAtt", "high", "treblePresence", "fluxHigh"}},
        {"motionAmount", {"speed", "warp", "turbulence"}},
        {"motionBreath", {"zoom"}},
        {"motionHue", {"colorShift"}},
        {"notePitch", {"knobs"}},
        {"noteIntervals", {"knobs"}},
        {"noteTension", {"knobs"}},
        {"noteChords", {"knobs"}},
        {"noteDrums", {"knobs"}},
    };
    if (field == "motionDrift") return catalogue::spinRate(styleId) != 0.0f;
    // TrailPass echoes (zooms and warps) the kept frame of any scene that keeps one (Scene::trailRetention).
    if (field == "trailZoom" || field == "trailWarp") return words.count("trailRetention") != 0;
    if (compositeApplies(kind, field)) return true;
    const auto via = kVia.find(field);
    if (via == kVia.end()) return words.count(field) != 0;
    for (const char* w : via->second) {
        if (compositeApplies(kind, w) || words.count(w) != 0) return true;
    }
    return false;
}

// Every control Customize shows on a style moves it: a shader style reads the uniform that carries the
// control's field, a dedicated renderer's sources read the field or the composite applies it, or a shared
// stage applies it to every style. --matrix prints each style's unread fields, the gaps file's format.
void everyShownControlReachesItsStyle(const std::string& assetsRoot, const std::string& coreRoot, const std::string& gapsPath,
                                      bool printMatrix) {
    static const std::set<std::string> kTrailFields = {"trails", "trailLength", "trailZoom", "trailWarp"};
    AAssetManager assets{assetsRoot};
    const ShaderSource source(&assets);
    auto listed = readGaps(gapsPath);
    std::map<catalogue::Kind, std::set<std::string>> kindWords;
    std::map<std::string, int> missingByField;
    int styles = 0;
    int cells = 0;
    int gaps = 0;
    for (const std::string& id : catalogue::allIds()) {
        const catalogue::Kind kind = catalogue::kindOf(id);
        const std::string present = presentShader(kind);
        // A 3D present is a style-like shader on the shared block: held to the shader carriers.
        const bool shader = kind == catalogue::Kind::Shader || !present.empty();
        std::set<std::string> words;
        std::set<std::string> heard;
        if (shader) {
            const auto src = source.load(present.empty() ? id + "_frag.glsl" : present, nullptr);
            if (!src) continue;
            words = wordsReachedFromMain(*src);
            heard = wordsReachedFromMain(*src, kViews);
        } else {
            if (kindWords.count(kind) == 0) {
                for (const auto& file : kindSources(kind)) {
                    const std::string text = readFile(coreRoot + file);
                    check(!text.empty(), "cannot read " + coreRoot + file);
                    kindWords[kind].merge(wordsOf(stripComments(text)));
                }
            }
            words = kindWords[kind];
        }
        ++styles;
        std::string missing;
        for (const auto& [field, control] : shownFields(id)) {
            if (sharedStageFields().count(field) != 0 && (shader || kTrailFields.count(field) == 0)) continue;
            if (shader) check(carrierOf(field) != nullptr, id + ": " + field + " (" + control + ") has no carrier; classify it in style_reads_test");
            ++cells;
            if (shader ? readsField(id, field, words, heard) : kindReads(kind, id, field, words)) continue;
            ++gaps;
            ++missingByField[field];
            missing += " " + field;
            check(listed[id].erase(field) == 1, id + ": shows " + control + " but never reads " + field);
        }
        if (printMatrix && !missing.empty()) std::printf("%s:%s\n", id.c_str(), missing.c_str());
    }
    for (const auto& [id, fields] : listed) {
        for (const auto& field : fields) check(false, id + ": now reads " + field + "; delete it from " + gapsPath);
    }
    if (printMatrix) {
        std::printf("# %d styles, %d style x field cells to read, %d not read\n", styles, cells, gaps);
        for (const auto& [field, n] : missingByField) std::printf("# %s: %d styles\n", field.c_str(), n);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool matrix = false;
    std::string root = "app/src/main/assets/";
    std::string gaps = "core/tests/style_param_gaps.txt";
    std::string core = "core/";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--matrix") == 0) {
            matrix = true;
        } else if (std::strcmp(argv[i], "--gaps") == 0 && i + 1 < argc) {
            gaps = argv[++i];
        } else if (std::strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            core = argv[++i];
        } else {
            root = argv[i];
        }
    }
    theReaderFollowsCallsNotDeclarations();
    theCatalogueFollowsTheShaders(root);
    everyShaderStyleReadsTheLookControls(root);
    thePrototypeReaderFindsOnlyPrototypes();
    noStyleDeclaresAFunctionAhead(root);
    everyShownControlReachesItsStyle(root, core, gaps, matrix);
    if (failures == 0) std::printf("style_reads_test: ok\n");
    return failures == 0 ? 0 : 1;
}
