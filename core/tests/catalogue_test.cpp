// Host-side checks for the scene and style catalogue (core/viz/Catalogue). Built and run by CI
// (.github/workflows/android.yml, "Catalogue host tests") with the system compiler; no GL.
//
//   catalogue_test core/tests/catalogue.txt           checks, including text() against the fixture
//   catalogue_test --write core/tests/catalogue.txt   rewrites the fixture after a deliberate change
//
// The fixture is what the JVM tests parse (they cannot load libgeode), so this test is what keeps
// them honest. The expectations below were taken from the Kotlin tables this catalogue replaced
// (VisualStyleCatalog, StyleFamilies, SceneCapabilities, ParamScope), so the port changed no number.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/scenes/StyleCatalog.hpp"

using namespace geode::viz;
using catalogue::Kind;
using catalogue::Scope;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

std::string join(std::initializer_list<std::string> parts) {
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += ' ';
        out += p;
    }
    return out;
}

// One line per style in the old Kotlin field order (label dropped, null geometry = -1).
std::string describe(const std::string& id) {
    using std::to_string;
    if (const auto* s = styles::cymatics(id)) {
        return join({"cymatics", s->id, to_string(s->shaderStyle), to_string(s->geometryOverride), num(s->scale), num(s->fill),
                     num(s->line), num(s->glow), num(s->iridescence), num(s->caustic), num(s->flow), num(s->swirl),
                     num(s->hueOffset), num(s->hueSpan), to_string(s->modeCap)});
    }
    if (const auto* s = styles::silk(id)) {
        return join({"silk", s->id, to_string(s->field), num(s->flow), num(s->fieldScale), num(s->strokes), num(s->elong),
                     num(s->decay), to_string(s->fold), num(s->swirl), num(s->exposure), num(s->hueOffset), num(s->hueSpan),
                     num(s->bBase), num(s->bAmp), num(s->bPeriod), num(s->slabRate)});
    }
    if (const auto* s = styles::life(id)) {
        return join({"life", s->id, to_string(s->rule), num(s->dt), to_string(s->core), to_string(s->growth), num(s->mu),
                     num(s->sigma), num(s->radius), to_string(s->rings), num(s->b1), num(s->b2), num(s->b3), num(s->feed),
                     num(s->kill), num(s->aniso), to_string(s->substeps), to_string(s->look), num(s->seedJitter),
                     num(s->hueOffset), num(s->hueSpan)});
    }
    if (const auto* s = styles::acid(id)) {
        return join({"acid", s->id, to_string(s->mode), to_string(s->source), num(s->zoom), num(s->rotate), num(s->hueRate),
                     num(s->feedback), num(s->modulate), num(s->glitch), num(s->overdrive), num(s->liquid), num(s->scanline),
                     num(s->curve), num(s->saturation), num(s->hueOffset), num(s->hueSpan)});
    }
    if (const auto* s = styles::myco(id)) {
        return join({"myco", s->id, to_string(s->agentRes), num(s->sensorDist), num(s->sensorAngle), num(s->turnAngle),
                     num(s->moveStep), num(s->jitter), num(s->deposit), num(s->decay), num(s->speciesMix), num(s->selfA),
                     num(s->crossAb), num(s->crossBa), num(s->selfB), num(s->snap), num(s->reaim), num(s->aniso),
                     to_string(s->look), num(s->exposure), num(s->hueOffset), num(s->hueSpan)});
    }
    if (const auto* s = styles::fluid(id)) {
        return join({"fluid", s->id, to_string(s->look), num(s->curl), num(s->velocityDissipation), num(s->densityDissipation),
                     num(s->splatRadius), num(s->splatForce), num(s->bloom), num(s->hueOffset)});
    }
    return "missing " + id;
}

// The old VisualStyleCatalog.kt, every row, in its order.
const char* const kOldTuning[] = {
    "cymatics cymatics 0 -1 1 1 1 1 1 1 1 1 0 1 8",
    "cymatics chladni_sand 1 1 1.08 0.2 1.28 0.58 0.18 0.25 0.35 1 0.06 0.45 8",
    "cymatics bessel_drum 2 0 0.92 1.15 0.88 0.85 0.45 0.9 1 1 -0.04 0.7 2",
    "cymatics harmonograph 3 -1 0.82 0.42 1.3 1.25 0.8 0.35 1.35 1.4 0.18 1.6 8",
    "cymatics faraday 4 0 1.18 1.1 0.78 1.08 0.65 1.2 1.55 1 -0.12 1.15 8",
    "cymatics harmonic_shell 5 0 0.72 1.2 0.72 0.95 1.25 1.3 1 0.45 0.1 1.35 8",
    "cymatics caustic_sheet 6 -1 1.05 1.28 0.55 0.88 1.3 1.65 1.2 1 -0.22 0.5 4",
    "cymatics levitator 7 1 0.88 0.45 0.92 1.42 0.6 0.72 0.55 1 0.32 0.8 3",
    "cymatics standing_chamber 8 1 0.78 0.75 0.92 1.18 0.5 0.8 0.65 1 -0.3 0.9 4",
    "cymatics rosensweig 9 0 1.15 1.35 0.65 1 0.35 1.45 0.48 1 0.45 0.35 8",
    "cymatics kundt_tube 10 1 0.7 0.68 1.25 0.9 0.35 0.55 1.1 1 0.03 0.6 8",
    "silk silk_web 0 1 1 1 1 0.985 0 0.25 1.35 0 1 0.17 0.05 37 0.02",
    "silk silk_bloom 1 0.85 0.8 1 1 0.988 0 0.15 1.35 0.55 1 0.16 0.05 41 0.02",
    "silk silk_weave 2 1.15 1.25 1.3 0.7 0.975 0 0.25 1.35 0.12 1 0.16 0.045 47 0.02",
    "silk silk_shell 3 0.9 0.9 0.8 1.4 0.987 0 0.4 1.35 -0.18 1 0.18 0.035 38 0.02",
    "silk silk_spiral 4 1.2 1.05 1 1.8 0.982 0 0.5 1.35 0.3 1 0.17 0.045 49 0.02",
    "silk silk_fold 5 1 1.15 1.2 1 0.98 0 0.25 1.35 0.78 1 0.2 0.04 44 0.02",
    "silk silk_hyper 6 0.8 0.75 1 2.2 0.99 0 0.25 1.35 0.48 1 0.22 0.05 35 0.02",
    "silk silk_resonance 7 1.05 1 1 1 0.985 0 0.3 1.35 0.06 1 0.19 0.04 43 0.02",
    "silk silk_curl 8 1.3 1.2 1.4 0.9 0.978 0 0.25 1.35 0.62 1 0.17 0.05 37 0.02",
    "silk silk_pendulum 9 0.95 0.85 1 1.5 0.986 3 0 1.35 0.9 1 0.17 0.05 37 0.02",
    "life life_orbium 0 0.1 0 0 0.15 0.017 13 1 1 0 0 0 0 0 1 0 9 0 1",
    "life life_gyre 0 0.1 0 0 0.156 0.0224 13 1 1 0 0 0 0 0 1 3 9 0.5 1",
    "life life_helix 0 0.1 0 0 0.3 0.0505 13 1 1 0 0 0 0 0 1 2 7 0.12 1",
    "life life_pulsar 0 0.1 0 0 0.38 0.07 13 1 1 0 0 0 0 0 1 0 6 0.07 1",
    "life life_hydro 0 0.1 1 1 0.26 0.036 18 3 1 1 1 0 0 0 1 2 5 0.4 1",
    "life life_bug 0 1 2 0 0.31 0.049 13 1 1 0 0 0 0 0 1 4 8 0.85 1",
    "life life_mitosis 1 1 0 0 0.15 0.017 13 1 1 0 0 0.0367 0.0649 0 4 0 11 0.6 1",
    "life life_coral 1 1 0 0 0.15 0.017 13 1 1 0 0 0.0545 0.062 0 5 2 8 0.02 1",
    "life life_labyrinth 1 1 0 0 0.15 0.017 13 1 1 0 0 0.026 0.055 0 5 1 7 0.09 1",
    "life life_worms 1 1 0 0 0.15 0.017 13 1 1 0 0 0.078 0.061 0 4 5 10 0 1",
    "acid acid_tv 0 0 1.01 0.0015 0.04 0.955 0.35 0.3 0.8 0.8 0.25 0.3 1.15 0 1",
    "acid acid_well 1 1 1.035 0.001 0.015 0.965 0.2 0 0 0 0.45 0.5 0.8 0.33 1",
    "acid acid_kaleid 2 0 1.014 0.002 0.05 0.95 0.5 0 0 0.5 0 0 1.1 0 1",
    "acid acid_droste 3 1 1 0.0012 0.03 0.96 0.3 0 0 0 0 0 1.05 0.72 1",
    "acid acid_prism 4 1 1.008 -0.0014 0.02 0.96 0.35 0 0 0 0 0 1.05 0.45 1",
    "acid acid_mosh 5 2 1.004 0 0.06 0.945 0.35 1 0.5 0 0 0 1.05 0.18 1",
    "acid acid_scan 6 2 1.006 0 0.03 0.95 0.35 0.7 0 0 0.8 0.6 1.05 0.55 1",
    "acid acid_solar 7 1 1.012 0.0018 0.08 0.93 0.35 0 0.3 0 0 0 1.05 0.06 1",
    "acid acid_mirror 8 0 1.009 0.0008 0.035 0.958 0.45 0 0 0 0 0 1.05 0.85 1",
    "acid acid_smear 9 3 1.005 0.0005 0.045 0.962 0.35 0 0.6 0.3 0 0 1.05 0.6 1",
    "myco myco_polycephalum 192 9 0.3927 0.7854 1 0.06 0.12 0.905 0 1 0 0 1 0 0.15 0 0 3.4 0 1",
    "myco myco_rivals 192 11 0.3927 0.7854 1 0.06 0.12 0.905 0.5 1 -1 -1 1 0 0.2 0 1 2.6 0 1.2",
    "myco myco_symbiosis 192 8 0.45 0.7854 1 0.06 0.12 0.905 0.5 1 0.35 0.35 1 0 0.15 0 0 3.4 0.1 1",
    "myco myco_predator 176 12 0.3927 0.7854 1.25 0.06 0.12 0.905 0.35 0.35 1.6 -1.3 1 0 0.25 0 4 3.4 0.03 1",
    "myco myco_ghosts 160 14 0.3927 0.7854 1 0.12 0.05 0.955 0 1 0 0 1 0 0 0 2 4.2 0.58 1",
    "myco myco_circuit 176 9 0.6 0.7854 1 0 0.12 0.87 0 1 0 0 1 0.7854 0 0 3 3.2 0.35 1",
    "myco myco_silkroad 176 22 0.18 0.12 1.35 0.02 0.1 0.93 0 1 0 0 1 0 0 0 1 3.4 -0.2 1",
    "myco myco_sporestorm 192 8 0.3927 0.7854 1 0.06 0.16 0.87 0 1 0 0 1 0 0.5 0 0 3.4 0.68 1",
    "myco myco_capillary 208 5 0.85 1.1 0.75 0.06 0.14 0.9 0 1 0 0 1 0 0 0 5 3.8 0.98 1",
    "myco myco_frostvein 176 10 0.35 0.5 0.9 0.06 0.12 0.915 0 1 0 0 1 0 0 0.8 1 3.4 0.52 1",
    "fluid fluid 0 1 1 1 1 1 1 0",
    "fluid fluid_ink 1 0.6 1.6 0.5 1.3 0.7 0.4 0.58",
    "fluid fluid_oilslick 2 1.3 0.7 0.6 1.2 1 0.8 0.12",
    "fluid fluid_neon 3 1.1 1 0.8 0.9 1.2 1.4 0.72",
    "fluid fluid_chrome 4 0.8 1.2 0.45 1.4 0.8 0.6 0.5",
    "fluid fluid_smoke 5 1.5 0.5 1.4 1.6 0.6 0.3 0.62",
    "fluid fluid_lava 6 0.5 2 0.35 1.5 0.5 1.5 0.02",
    "fluid fluid_marble 7 0.9 1.4 0.3 1.1 0.75 0.5 0.33",
    "fluid fluid_aurora 8 1.4 0.6 0.9 1.3 0.9 1.2 0.4",
};

void everyOldStyleKeepsItsNumbersAndKind() {
    for (const char* row : kOldTuning) {
        const std::string expected = row;
        const std::string tag = expected.substr(0, expected.find(' '));
        const std::string rest = expected.substr(tag.size() + 1);
        const std::string id = rest.substr(0, rest.find(' '));
        check(describe(id) == expected, "tuning of " + id + ": got [" + describe(id) + "]");
        check(catalogue::knows(id), id + " is known");
        const Kind want = tag == "cymatics" ? Kind::Cymatics
                          : tag == "silk"   ? Kind::Silk
                          : tag == "life"   ? Kind::Life
                          : tag == "acid"   ? Kind::Acid
                          : tag == "myco"   ? Kind::Mycelium
                                            : Kind::Fluid;
        check(catalogue::kindOf(id) == want, "kind of " + id);
    }
    // Same lists, same order: the pickers list them as the tables do.
    check(styles::silkIds().size() == 10 && styles::lifeIds().size() == 10 && styles::mycoIds().size() == 10 &&
              styles::acidIds().size() == 10 && styles::fluidIds().size() == 9 && styles::cymaticsIds().size() == 11,
          "per-kind style counts");
}

void familiesHaveTwelveStylesAndAnOpener() {
    const char* const openers[] = {"arbor_grove", "bulb_classic", "chrys_bloom",  "lucid_weave", "dive_lattice",
                                   "glass_orb",   "nebula_dust",  "terra_ridge", "truchet_arcs", "foam_fall"};
    const char* const names[] = {"arbor", "bulb", "chrysanthemum", "lucid", "dive", "glass", "nebula", "terrain", "truchet", "foam"};
    std::set<std::string> seen;
    int i = 0;
    for (const auto& family : catalogue::families()) {
        check(std::string(family.name) == names[i], std::string("family order at ") + names[i]);
        check(std::string(family.opener()) == openers[i], std::string("opener of ") + family.name);
        check(std::string(family.library) == std::string("lib_") + std::string(family.prefix).substr(0, std::strlen(family.prefix) - 1),
              std::string("library of ") + family.name);
        for (const char* id : family.ids) {
            check(std::string(id).rfind(family.prefix, 0) == 0, std::string(id) + " starts with its prefix");
            check(catalogue::isShaderStyle(id) && catalogue::kindOf(id) == Kind::Shader, std::string(id) + " is a shader style");
            seen.insert(id);
        }
        ++i;
    }
    check(i == 10, "ten families");
    check(seen.size() == 10 * 12, "120 distinct family styles");
}

void spinRateAndShaderStyleAgree() {
    const std::set<std::string> oldSpinning = {
        "kaleido", "mandala_dome", "orb_lattice", "chroma_orb", "kifs", "spiral", "spiral_eye",
        "bead_vortex", "merkaba_grid", "tunnel", "rod_tunnel", "particle_tunnel", "lucid_mandala", "lucid_mirror",
        "lucid_spiral", "lucid_vortex", "lucid_tunnel", "lucid_disc", "chrys_kaleido", "chrys_tunnel"};
    std::set<std::string> spinning;
    int shaders = 0;
    for (const auto& id : catalogue::allIds()) {
        if (catalogue::spinRate(id) != 0.0f) spinning.insert(id);
        if (catalogue::isShaderStyle(id)) ++shaders;
    }
    check(spinning == oldSpinning, "the spinning styles are the old SPINNING_SCENES");
    for (const auto& id : spinning) check(catalogue::isShaderStyle(id), id + " spins and is a shader style");
    check(shaders == 175, "175 shader styles (55 loose + 120 family), got " + std::to_string(shaders));
    check(catalogue::spinRate("bead_vortex") == 1.4f && catalogue::spinRate("tunnel") == -0.5f, "spin rates kept");
    for (const char* id : {"silk_web", "milkdrop", "fluid", "water", "curlflow", "cymatics", "no_such_style"}) {
        check(!catalogue::isShaderStyle(id), std::string(id) + " is not a shader style");
    }
    check(catalogue::allIds().size() == 238, "238 ids");
    check(catalogue::allIds().front() == "silk_web", "the default comes first");
}

void marchedStylesMatchTheOldSet() {
    const std::set<std::string> old = {
        "arbor_bonsai", "arbor_canopy", "arbor_coral", "arbor_spire", "arbor_synapse", "arbor_willow", "aurora_ocean",
        "bulb_cavern", "bulb_chrome", "bulb_classic", "bulb_contour", "bulb_glow", "bulb_hybrid", "bulb_ink", "bulb_julia",
        "bulb_lotus", "bulb_prism", "bulb_shell", "bulb_slice", "chrome_nebula", "chrys_corolla", "crystal_canyon",
        "curl_bloom", "dive_accordion", "dive_cycle", "dive_gyro", "dive_helix", "dive_jewel", "dive_lattice",
        "dive_mandala", "dive_mushroom", "dive_neon", "dive_orbit", "dive_serpent", "dive_show", "fractal_dive", "fractal_tunnel",
        "glass_cubes", "glass_gem", "glass_ice", "glass_knot", "gyroid_cavern", "jelly_abyss", "kifs", "menger_core",
        "morphogen", "nebula", "nebula_flight", "nebula_pillars", "nebula_storm", "nebula_web", "nectar_flow", "neon_city",
        "noneuclid", "particle_tunnel", "prism_rain", "rod_tunnel", "spectrum_terrain", "supershape", "terra_canyon", "terra_dunes",
        "terra_glacier", "terra_isles", "terra_mesa", "terra_moon", "terra_night", "terra_ridge", "terra_terraces",
        "terra_volcano", "terra_wire", "truchet_relief", "vanishing",
        "foam_fall", "foam_neon", "foam_soap", "foam_chrome", "foam_ember", "foam_ice",
        "foam_orb", "foam_kaleido", "foam_ink", "foam_coral", "foam_ghost", "foam_show"};
    std::set<std::string> marched;
    for (const auto& id : catalogue::allIds()) {
        if (catalogue::isMarched(id)) marched.insert(id);
    }
    check(marched == old, "the marched styles are the old MARCHED_SCENES");
}

void paramScopesMatchTheOldParamScope() {
    check(catalogue::scopeOf("Detail") == Scope::MarchDetail, "Detail");
    check(catalogue::scopeOf("Speed") == Scope::SceneClock, "Speed");
    check(catalogue::scopeOf("Zoom") == Scope::Universal, "Zoom");
    check(catalogue::scopeOf("Morph") == Scope::ShaderLook, "Morph");
    check(catalogue::scopeOf("Fluid curl") == Scope::FluidSim, "Fluid curl");
    check(catalogue::scopeOf("Radius on hit") == Scope::Emitters, "Radius on hit");
    check(catalogue::scopeOf("Trail warp (liquid echo)") == Scope::TrailEcho, "Trail warp");
    check(catalogue::scopeOf("Ripple glint") == Scope::RippleOverlay, "Ripple glint");
    check(catalogue::scopeOf("Caustic sheen") == Scope::Cymatics, "Caustic sheen");
    check(catalogue::scopeOf("Flow force") == Scope::FlowFieldSim, "Flow force");
    check(catalogue::scopeOf("not a control") == Scope::Universal, "unlisted keys are universal");

    check(catalogue::reaches(Scope::Turbulence, "plasma"), "turbulence reaches a shader");
    check(!catalogue::reaches(Scope::Turbulence, "silk_web"), "turbulence misses silk");
    check(catalogue::reaches(Scope::MarchDetail, "vanishing"), "detail reaches a marched style");
    check(!catalogue::reaches(Scope::MarchDetail, "plasma"), "detail misses an unmarched shader");
    check(!catalogue::reaches(Scope::FlowFieldSim, "fluid_ink"), "fluid drives its own field");
    check(catalogue::reaches(Scope::FlowFieldSim, "silk_web"), "flow field reaches silk");
    check(!catalogue::reaches(Scope::RippleOverlay, "water"), "water has no ripple overlay");
    check(!catalogue::reaches(Scope::ShaderLook, "milkdrop"), "milkdrop has no shader look");
    check(catalogue::reaches(Scope::EndlessZoom, "milkdrop"), "milkdrop dives");
    check(!catalogue::reaches(Scope::SceneClock, "milkdrop"), "milkdrop paces itself");
    check(!catalogue::reaches(Scope::BassBand, "life_orbium") && catalogue::reaches(Scope::TrebleBand, "life_orbium"),
          "life reads treble, not bass");
    check(catalogue::reaches(Scope::Journey, "curlflow") && !catalogue::reaches(Scope::Emitters, "curlflow"), "curl flow");
    check(catalogue::reaches(Scope::Cymatics, "kundt_tube") && !catalogue::reaches(Scope::Cymatics, "plasma"), "cymatics");
    check(catalogue::kindOf("no_such_style") == Kind::Silk, "unknown ids are silk");
    check(catalogue::reaches(Scope::TrailToggle, "no_such_style"), "an unknown id is described as silk");
    for (const auto& id : catalogue::allIds()) check(catalogue::reaches(Scope::Universal, id), id + " is reached by universal");
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(s);
    while (std::getline(in, cur, sep)) out.push_back(cur);
    return out;
}

void textRoundTrips() {
    const auto lines = split(catalogue::text(), '\n');
    check(!lines.empty() && lines[0] == "catalogue|" + std::to_string(catalogue::kTextVersion), "versioned header");
    std::vector<std::string> styleIds;
    int kinds = 0, families = 0, keys = 0, overrides = 0;
    for (const auto& line : lines) {
        const auto f = split(line, '|');
        if (f[0] == "style") {
            styleIds.push_back(f[1]);
            check(f[2] == catalogue::name(catalogue::kindOf(f[1])), "kind row of " + f[1]);
            check((f.size() == 4 && f[3] == "MARCH_DETAIL") == catalogue::isMarched(f[1]), "marched field of " + f[1]);
        } else if (f[0] == "kind") {
            ++kinds;
        } else if (f[0] == "family") {
            ++families;
            check(split(f[4], ',').size() == 12, "family row lists twelve ids");
        } else if (f[0] == "override") {
            ++overrides;
            check(styles::cymatics(f[1]) && f[2] == "Geometry" && std::stoi(f[3]) == styles::cymatics(f[1])->geometryOverride,
                  "geometry override of " + f[1]);
        } else if (f[0] == "key") {
            ++keys;
            check(f[2] == catalogue::name(catalogue::scopeOf(f[1])), "scope row of " + f[1]);
            check(catalogue::scopeOf(f[1]) != Scope::Universal, "universal keys are implicit");
        }
    }
    check(styleIds == catalogue::allIds(), "style rows in picker order");
    check(kinds == catalogue::kKindCount && families == 10 && keys == 80 && overrides == 8, "row counts");
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    std::string out;
    for (char c : text) {
        if (c != '\r') out += c;  // a Windows checkout
    }
    return out;
}

void textEqualsTheFixture(const char* path) {
    const std::string fixture = readFile(path);
    check(!fixture.empty(), std::string("fixture readable: ") + path);
    if (fixture == catalogue::text()) return;
    const auto want = split(fixture, '\n');
    const auto got = split(catalogue::text(), '\n');
    for (size_t i = 0; i < std::max(want.size(), got.size()); ++i) {
        const std::string w = i < want.size() ? want[i] : "<end>";
        const std::string g = i < got.size() ? got[i] : "<end>";
        if (w != g) {
            check(false, "text() differs from " + std::string(path) + " at line " + std::to_string(i + 1) + ": fixture [" + w +
                             "] native [" + g + "]; after a deliberate change run catalogue_test --write " + path);
            return;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << catalogue::text();
        return 0;
    }
    everyOldStyleKeepsItsNumbersAndKind();
    familiesHaveTwelveStylesAndAnOpener();
    spinRateAndShaderStyleAgree();
    marchedStylesMatchTheOldSet();
    paramScopesMatchTheOldParamScope();
    textRoundTrips();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/catalogue.txt");
    if (failures == 0) std::printf("catalogue_test: all checks passed\n");
    return failures;
}
