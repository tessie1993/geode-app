// Host-side checks for the bundled looks (core/viz/CataloguePresets, #648): the twelve generic looks
// every loose shader style offers and the hand-tuned variants, as the shell reads them through
// geode_catalogue_presets_text. Built and run by CI (.github/workflows/android.yml, "Catalogue host
// tests") with the system compiler; no GL.
//
// The expected values are the literals of the Kotlin tables this replaced (ui/BuiltInPresets.kt's
// LOOKS, SHADER_VARIANTS, FLUID_VARIANTS and CYMATICS_VARIANTS), so the port changed no number: a
// preset's params are SceneParams{} with the row's values set by name, exactly as the Kotlin
// SceneParams(...) literal was its defaults with the named arguments.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/CataloguePresets.hpp"
#include "viz/Params.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

const catalogue::Preset* find(const std::string& name) {
    for (const auto& p : catalogue::presets()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// Every wire slot of a and b holds the same bits.
bool sameParams(const SceneParams& a, const SceneParams& b) {
    float wa[SceneParams::kFieldCount];
    float wb[SceneParams::kFieldCount];
    a.toWire(wa);
    b.toWire(wb);
    return std::memcmp(wa, wb, sizeof wa) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(s);
    while (std::getline(in, part, sep)) out.push_back(part);
    if (!s.empty() && s.back() == sep) out.emplace_back();
    return out;
}

void testGenericLookOnEveryLooseShaderStyle() {
    std::vector<std::string> loose;
    std::set<std::string> familyIds;
    for (const auto& family : catalogue::families()) familyIds.insert(family.ids.begin(), family.ids.end());
    for (const auto& id : catalogue::allIds()) {
        if (catalogue::kindOf(id) == catalogue::Kind::Shader && !familyIds.count(id)) loose.push_back(id);
    }
    const char* looks[] = {"Chill", "Punchy", "Hypno", "Vivid", "Retro", "Glitch",
                           "Dream", "Warp",   "Prism", "Noir",  "Strobe", "Deep"};
    const auto& all = catalogue::presets();
    check(all.size() == loose.size() * 12 + 31, "12 looks per loose shader style plus 31 variants, got " + std::to_string(all.size()));
    // Style-major, looks in their order, then the variants.
    size_t i = 0;
    for (const auto& id : loose) {
        for (const char* look : looks) {
            if (i >= all.size()) break;
            check(all[i].name == id + " · " + look, "preset " + std::to_string(i) + " is " + id + " · " + look + ", got " + all[i].name);
            check(all[i].sceneId == id, all[i].name + " plays " + id);
            ++i;
        }
    }
    check(i < all.size() && all[i].name == "orb_lattice · Deep Sea", "the variants follow the looks");
    for (const auto& p : all) {
        check(!familyIds.count(p.sceneId), p.name + " is not on a style family's style");
        check(catalogue::knows(p.sceneId), p.name + " plays a style the catalogue knows: " + p.sceneId);
    }
}

void testChillPinned() {
    const auto* p = find("julia · Chill");
    check(p != nullptr, "julia · Chill exists");
    if (!p) return;
    check(sameBits(p->attack, 0.4f) && sameBits(p->decay, 0.06f), "Chill attack and decay");
    const SceneParams& s = p->params;
    check(sameBits(s.speed, 0.5f) && sameBits(s.audioDrive, 0.85f) && sameBits(s.beatResponse, 0.45f), "Chill motion");
    check(s.trails && sameBits(s.trailLength, 0.8f) && s.colorCycle && sameBits(s.cycleSpeed, 0.025f), "Chill trails and cycle");
    check(s.palette == 3 && sameBits(s.sway, 0.35f) && sameBits(s.vignette, 0.4f) && sameBits(s.paramFadeSec, 1.5f), "Chill look");
    check(sameBits(s.saturation, 0.8f) && sameBits(s.brightness, 0.9f), "Chill grade");
    // What the Kotlin literal left out is SceneParams' default.
    const SceneParams d{};
    check(sameBits(s.zoom, d.zoom) && s.palette2 == d.palette2 && s.kaleidoscope == d.kaleidoscope, "Chill leaves the rest at default");
    // Every loose style's Chill is the same look.
    const auto* other = find("lava · Chill");
    check(other && sameParams(other->params, s) && sameBits(other->attack, 0.4f), "lava · Chill is julia's Chill");
}

void testVariantsPinned() {
    const auto* deep = find("orb_lattice · Deep Sea");
    check(deep && deep->sceneId == "orb_lattice" && sameBits(deep->attack, 0.45f) && sameBits(deep->decay, 0.1f), "Deep Sea row");
    if (deep) {
        const SceneParams& s = deep->params;
        check(s.palette == 3 && s.palette2 == 6 && sameBits(s.paletteMix, 0.35f) && sameBits(s.chromaAb, 0.2f) &&
                  sameBits(s.paramFadeSec, 1.0f),
              "Deep Sea values");
    }
    const auto* ink = find("fluid · Inkdrop");
    check(ink && ink->sceneId == "fluid" && sameBits(ink->attack, 0.35f) && sameBits(ink->decay, 0.5f), "Inkdrop row");
    if (ink) {
        const SceneParams& s = ink->params;
        check(s.fluidBeatPattern == 0 && s.fluidBeatSplats == 2 && s.fluidStirrers == 1 && sameBits(s.fluidCurl, 20.0f) &&
                  sameBits(s.fluidDensityDissipation, 0.35f) && sameBits(s.fluidSplatRadius, 0.16f) && !s.fluidParticlesEnabled &&
                  s.palette == 5 && s.fluidSpawnPath == 4 && s.fluidSpawnPoints == 1 && s.fluidCatchPoints == 0,
              "Inkdrop values");
    }
    const auto* plate = find("cymatics · Chladni plate");
    check(plate && plate->sceneId == "cymatics" && sameBits(plate->attack, 0.35f) && sameBits(plate->decay, 0.45f), "Chladni row");
    if (plate) {
        const SceneParams& s = plate->params;
        check(s.cymaticsGeometry == 1 && sameBits(s.cymaticsFundamental, 82.0f) && s.cymaticsModes == 4 &&
                  sameBits(s.cymaticsScale, 1.15f) && sameBits(s.cymaticsSwirl, 0.0f) && s.palette == 4 && sameBits(s.hueRange, 0.6f),
              "Chladni values");
    }
    const auto* sumi = find("fluid_ink · Sumi");
    check(sumi && sumi->sceneId == "fluid_ink" && sameBits(sumi->decay, 0.6f), "a styled fluid look's preset names its style");
    int styledFluid = 0;
    for (const auto& p : catalogue::presets()) styledFluid += p.sceneId.rfind("fluid_", 0) == 0;
    check(styledFluid == 5, "one preset per styled fluid look");
}

// The text the shell parses rebuilds every preset bit for bit: SceneParams{} with each look row's
// values set by name, the way Kotlin's reader starts from SceneParams() and sets slots by name.
void testTextRoundTrips() {
    const std::string& text = catalogue::presetsText();
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);
    const auto header = split(line, '|');
    check(header.size() == 4 && header[0] == "presets" && header[1] == std::to_string(catalogue::kPresetsTextVersion),
          "header: " + line);
    struct Look {
        float attack;
        float decay;
        SceneParams params;
    };
    std::map<std::string, Look> looks;
    std::vector<catalogue::Preset> read;
    while (std::getline(in, line)) {
        const auto f = split(line, '|');
        if (f[0] == "look" && f.size() == 5) {
            Look look{std::strtof(f[2].c_str(), nullptr), std::strtof(f[3].c_str(), nullptr), SceneParams{}};
            for (const auto& pair : split(f[4], ',')) {
                const auto eq = pair.find('=');
                const std::string name = pair.substr(0, eq);
                const int slot = eq == std::string::npos ? -1 : SceneParams::slotOf(name);
                const float value = std::strtof(pair.c_str() + eq + 1, nullptr);
                check(slot >= 0, "look " + f[1] + " sets a known param: " + pair);
                if (slot < 0) continue;
                const ParamSpec& spec = SceneParams::spec(static_cast<ParamSlot>(slot));
                check(!SceneParams::retired(static_cast<ParamSlot>(slot)), "look " + f[1] + " sets no retired param: " + pair);
                check(value >= spec.min && value <= spec.max, "look " + f[1] + " stays in the param's range: " + pair);
                check(look.params.set(name, value), "look " + f[1] + " lands: " + pair);
            }
            check(!looks.count(f[1]), "look key " + f[1] + " is unique");
            looks[f[1]] = look;
        } else if (f[0] == "preset" && f.size() == 4) {
            check(looks.count(f[3]) == 1, f[1] + " names a look row above it: " + f[3]);
            const Look& look = looks[f[3]];
            read.push_back({f[1], f[2], look.attack, look.decay, look.params});
        } else {
            check(false, "unknown row: " + line);
        }
    }
    check(header.size() == 4 && std::to_string(looks.size()) == header[2] && std::to_string(read.size()) == header[3], "header counts");
    const auto& all = catalogue::presets();
    check(read.size() == all.size(), "the text lists every preset");
    for (size_t i = 0; i < read.size() && i < all.size(); ++i) {
        check(read[i].name == all[i].name && read[i].sceneId == all[i].sceneId, "row " + std::to_string(i) + " is " + all[i].name);
        check(sameBits(read[i].attack, all[i].attack) && sameBits(read[i].decay, all[i].decay), all[i].name + " envelope reads back");
        check(sameParams(read[i].params, all[i].params), all[i].name + " params read back bit for bit");
    }
}

}  // namespace

int main() {
    testGenericLookOnEveryLooseShaderStyle();
    testChillPinned();
    testVariantsPinned();
    testTextRoundTrips();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("presets_test: OK (%zu presets)\n", catalogue::presets().size());
    return 0;
}
