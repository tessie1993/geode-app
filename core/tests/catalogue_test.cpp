// Host-side checks for the scene and style catalogue (core/viz/Catalogue). Built and run by CI
// (.github/workflows/android.yml, "Catalogue host tests") with the system compiler; no GL.
//
//   catalogue_test core/tests/catalogue.txt           checks, including text() against the fixture
//   catalogue_test --write core/tests/catalogue.txt   rewrites the fixture after a deliberate change
//
// The fixture is the catalogue as one table, which the tools read (tools/store-stats.sh, tools/glslcheck).
// The shell reads none of it: it asks one question at a time, and the answers it gets are checked here
// against the same tables. The expectations below are the catalogue's numbers from before it lived in
// core, so no number moved.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/TouchField.hpp"
#include "viz/fluid/FluidQuality.hpp"
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

// One line per style in the order the rows below were written in (no label, no held geometry = -1).
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

// Every style's tuning row from before the tables lived in core, in that order.
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

void familiesCarryTheirOwnCountAndAnOpener() {
    const char* const openers[] = {"arbor_grove", "bulb_classic", "chrys_bloom",  "lucid_weave", "dive_lattice",
                                   "glass_orb",   "nebula_dust",  "terra_ridge", "truchet_arcs", "foam_fall",
                                   "flame_veil",  "opal_bubbles", "hollow_sinew", "prism_portal", "fluor_whorl",
                                   "loom_braid",  "shard_splinter", "petal_rose", "fold_lace"};
    const char* const names[] = {"arbor", "bulb", "chrysanthemum", "lucid", "dive", "glass", "nebula", "terrain", "truchet", "foam",
                                 "flame", "opal",  "hollow", "prism", "fluor", "loom", "shard", "petal", "fold"};
    // The first ten families have twelve styles, the ones since ten: the count is the family's own.
    const size_t counts[] = {12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 10, 10, 10, 10, 10, 10, 10, 10, 10};
    std::set<std::string> seen;
    int i = 0;
    for (const auto& family : catalogue::families()) {
        check(std::string(family.name) == names[i], std::string("family order at ") + names[i]);
        check(std::string(family.opener()) == openers[i], std::string("opener of ") + family.name);
        check(family.ids.size() == counts[i], std::string("style count of ") + family.name);
        check(std::string(family.library) == std::string("lib_") + std::string(family.prefix).substr(0, std::strlen(family.prefix) - 1),
              std::string("library of ") + family.name);
        for (const char* id : family.ids) {
            check(std::string(id).rfind(family.prefix, 0) == 0, std::string(id) + " starts with its prefix");
            check(catalogue::isShaderStyle(id) && catalogue::kindOf(id) == Kind::Shader, std::string(id) + " is a shader style");
            seen.insert(id);
        }
        ++i;
    }
    check(i == 19, "nineteen families");
    check(seen.size() == 10 * 12 + 9 * 10, "210 distinct family styles");
}

void spinRateAndShaderStyleAgree() {
    const std::set<std::string> oldSpinning = {
        "kaleido", "mandala_dome", "orb_lattice", "chroma_orb", "kifs", "spiral", "spiral_eye",
        "bead_vortex", "merkaba_grid", "tunnel", "rod_tunnel", "particle_tunnel", "lucid_mandala", "lucid_mirror",
        "lucid_spiral", "lucid_vortex", "lucid_tunnel", "lucid_disc", "chrys_kaleido", "chrys_tunnel",
        // The neon set's two turning looks, added after the port.
        "glow_vortex", "hex_hive"};
    std::set<std::string> spinning;
    int shaders = 0;
    for (const auto& id : catalogue::allIds()) {
        if (catalogue::spinRate(id) != 0.0f) spinning.insert(id);
        if (catalogue::isShaderStyle(id)) ++shaders;
    }
    check(spinning == oldSpinning, "the spinning styles are the old SPINNING_SCENES");
    for (const auto& id : spinning) check(catalogue::isShaderStyle(id), id + " spins and is a shader style");
    check(shaders == 272, "272 shader styles (62 loose + 210 family), got " + std::to_string(shaders));
    check(catalogue::spinRate("bead_vortex") == 1.4f && catalogue::spinRate("tunnel") == -0.5f, "spin rates kept");
    for (const char* id : {"silk_web", "milkdrop", "fluid", "curlflow", "cymatics", "no_such_style"}) {
        check(!catalogue::isShaderStyle(id), std::string(id) + " is not a shader style");
    }
    check(catalogue::allIds().size() == 334, "334 ids");
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
        "foam_orb", "foam_kaleido", "foam_ink", "foam_coral", "foam_ghost", "foam_show",
        "neon_chrome_sea", "black_hole", "hex_hive", "mandelbox_cathedral", "morph_world",
        "julia", "tunnel", "mandel", "kaleido", "plasma", "bars", "ring", "scope", "liss", "warp", "grid", "voronoi",
        "metaballs", "ripples", "starfield", "waves", "hexgrid", "spiral", "aurora", "solar", "winter", "lava",
        "orb_lattice", "neon_tiles", "chroma_orb", "mandala_dome", "bead_vortex", "merkaba_grid", "spiral_eye",
        "blacklight_bloom", "fractal_temple", "glow_vortex",
        "arbor_grove", "arbor_frost", "arbor_bolt", "arbor_roots", "arbor_rosette", "arbor_sakura", "chrys_bloom",
        "chrys_phyllo", "chrys_dahlia", "chrys_lotus", "chrys_meadow", "chrys_glass", "chrys_ink", "chrys_neon",
        "chrys_fall", "chrys_kaleido", "chrys_tunnel", "lucid_weave", "lucid_mandala", "lucid_spiral",
        "lucid_tunnel", "lucid_disc", "lucid_hive", "lucid_ink", "lucid_mirror", "lucid_bulge", "lucid_petal",
        "lucid_lattice", "lucid_vortex", "glass_orb", "glass_pool", "glass_prism", "glass_lens", "glass_drops",
        "glass_bubbles", "glass_marble", "glass_caustic", "nebula_dust", "nebula_spiral", "nebula_ring",
        "nebula_veil", "nebula_pulsar", "nebula_cluster", "nebula_nova", "nebula_horizon", "terra_topo",
        "truchet_arcs", "truchet_hex", "truchet_weave", "truchet_maze", "truchet_tri", "truchet_quad",
        "truchet_tubes", "truchet_neon", "truchet_flow", "truchet_knot", "truchet_tunnel",
        // The last three that faked their depth, remade as marches.
        "outrun_drive", "ringed_planet", "ribbon_storm"};
    std::set<std::string> want = old;
    // Every style of the families added since marches.
    for (const char* family : {"flame", "opal", "hollow", "prism", "fluor", "loom", "shard", "petal", "fold"}) {
        for (const auto& f : catalogue::families()) {
            if (std::string(f.name) == family) want.insert(f.ids.begin(), f.ids.end());
        }
    }
    // Life is presented as a marched field on the camera rig (core/viz/present/FieldPresent).
    for (const auto& id : styles::lifeIds()) want.insert(id);
    std::set<std::string> marched;
    for (const auto& id : catalogue::allIds()) {
        if (catalogue::isMarched(id)) marched.insert(id);
    }
    check(marched == want, "the marched styles are the old MARCHED_SCENES and the new families");
}

void paramScopesMatchTheOldParamScope() {
    check(catalogue::scopeOf("Detail") == Scope::MarchDetail, "Detail");
    check(catalogue::scopeOf("Speed") == Scope::SceneClock, "Speed");
    check(catalogue::scopeOf("Zoom") == Scope::Universal, "Zoom");
    check(catalogue::scopeOf("Morph") == Scope::ShaderLook, "Morph");
    check(catalogue::scopeOf("Fluid curl") == Scope::FluidSim, "Fluid curl");
    check(catalogue::scopeOf("Radius on hit") == Scope::Emitters, "Radius on hit");
    check(catalogue::scopeOf("Trail warp (liquid echo)") == Scope::TrailEcho, "Trail warp");
    check(catalogue::scopeOf("Caustic sheen") == Scope::Cymatics, "Caustic sheen");
    check(catalogue::scopeOf("Flow force") == Scope::FlowFieldSim, "Flow force");
    check(catalogue::scopeOf("not a control") == Scope::Universal, "unlisted keys are universal");

    check(catalogue::reaches(Scope::Turbulence, "plasma"), "turbulence reaches a shader");
    check(!catalogue::reaches(Scope::Turbulence, "silk_web"), "turbulence misses silk");
    check(catalogue::reaches(Scope::MarchDetail, "vanishing"), "detail reaches a marched style");
    check(!catalogue::reaches(Scope::MarchDetail, catalogue::kPool), "detail misses an unmarched shader");
    // Spin (the old Spin drift) shows only where a style turns: bars, grids, landscapes and trees hold still.
    check(catalogue::scopeOf("Spin") == Scope::Spin && catalogue::scopeOf("Spin drift") == Scope::Universal, "Spin, and no Spin drift");
    check(catalogue::reaches(Scope::Spin, "bead_vortex") && catalogue::reaches(Scope::Spin, "tunnel"), "spin reaches a turning style");
    check(!catalogue::reaches(Scope::Spin, "bars") && !catalogue::reaches(Scope::Spin, "terra_ridge") &&
              !catalogue::reaches(Scope::Spin, "arbor_grove") && !catalogue::reaches(Scope::Spin, "silk_web"),
          "spin misses a style that holds still");
    check(!catalogue::reaches(Scope::FlowFieldSim, "fluid_ink"), "fluid drives its own field");
    check(catalogue::reaches(Scope::FlowFieldSim, "silk_web"), "flow field reaches silk");
    check(!catalogue::reaches(Scope::ShaderLook, "milkdrop"), "milkdrop has no shader look");
    check(catalogue::reaches(Scope::EndlessZoom, "milkdrop") && catalogue::reaches(Scope::EndlessZoom, "silk_web") &&
              catalogue::reaches(Scope::EndlessZoom, "fluid_ink") && catalogue::reaches(Scope::EndlessZoom, "curlflow"),
          "every kind dives");
    check(!catalogue::reaches(Scope::SceneClock, "milkdrop"), "milkdrop paces itself");
    // Life's present is a ShaderScene on the shared block (core/viz/present): the bands, the look, Turbulence.
    check(catalogue::reaches(Scope::BassBand, "life_orbium") && catalogue::reaches(Scope::MidBand, "life_orbium") &&
              catalogue::reaches(Scope::TrebleBand, "life_orbium"),
          "life hears every band");
    check(catalogue::reaches(Scope::ShaderLook, "life_orbium") && catalogue::reaches(Scope::Turbulence, "life_orbium") &&
              catalogue::reaches(Scope::MarchDetail, "life_worms") && catalogue::fliesCamera("life_worms"),
          "life reads the shader look, Turbulence and Detail and flies the rig");
    check(!catalogue::reaches(Scope::ShaderLook, "silk_web") && !catalogue::fliesCamera("silk_web"), "silk is still flat");
    check(catalogue::reaches(Scope::Journey, "curlflow") && !catalogue::reaches(Scope::Emitters, "curlflow"), "curl flow");
    check(catalogue::reaches(Scope::Cymatics, "kundt_tube") && !catalogue::reaches(Scope::Cymatics, "plasma"), "cymatics");
    check(catalogue::kindOf("no_such_style") == Kind::Silk, "unknown ids are silk");
    check(catalogue::reaches(Scope::TrailToggle, "no_such_style"), "an unknown id is described as silk");
    // A look, playlist entry, take or template saved on Water, which Pool replaced, opens on Pool.
    check(!catalogue::knows("water") && catalogue::resolveId("water") == catalogue::kPool, "Water is gone and opens on Pool");
    check(catalogue::isShaderStyle(catalogue::kPool) && catalogue::lookOf(catalogue::kPool) == catalogue::Look::Flowing, "Pool is a flowing shader style");
    check(catalogue::resolveId("no_such_style") == "no_such_style", "an id never retired is left alone");
    for (const auto& r : catalogue::kRetired) check(catalogue::knows(r.now) && !catalogue::knows(r.id), std::string("retired to a real style: ") + r.id);
    for (const auto& id : catalogue::allIds()) check(catalogue::resolveId(id) == id, id + " resolves to itself");
    check(catalogue::text().find("\nretired|water|pool\n") != std::string::npos, "the shell reads the retired ids");
    for (const auto& id : catalogue::allIds()) check(catalogue::reaches(Scope::Universal, id), id + " is reached by universal");
}

// The Styles page's look groups, as the Kotlin StyleLookTags placed them before the tags moved here:
// a family style by its family, a dedicated renderer's style by its kind, a loose shader one by one,
// and an untagged loose style under Psychedelic (only Winter was).
const std::map<std::string, std::string> kOldFamilyLooks = {
    {"bulb", "FRACTALS"}, {"arbor", "FRACTALS"}, {"foam", "FRACTALS"},       {"dive", "FLY_THROUGH"},
    {"terrain", "FLY_THROUGH"}, {"nebula", "SPACE"}, {"glass", "GLASS"},     {"truchet", "PATTERNS"},
    {"lucid", "PSYCHEDELIC"},   {"chrysanthemum", "PSYCHEDELIC"},
    // Added after the tags moved here.
    {"flame", "PSYCHEDELIC"},   {"opal", "GLASS"},   {"hollow", "FLY_THROUGH"},
    {"prism", "GLASS"},         {"fluor", "FLY_THROUGH"}, {"loom", "FLY_THROUGH"}, {"shard", "GLASS"},
    {"petal", "FLOWING"},       {"fold", "FRACTALS"},
};
const std::map<Kind, std::string> kOldKindLooks = {
    {Kind::Silk, "FLOWING"}, {Kind::Fluid, "FLOWING"},     {Kind::CurlFlow, "FLOWING"},
    {Kind::Life, "LIVING"},  {Kind::Mycelium, "LIVING"},   {Kind::Acid, "PSYCHEDELIC"}, {Kind::Cymatics, "PATTERNS"},
    {Kind::Milkdrop, "CLASSIC"},
};
const std::map<std::string, std::string> kOldLooseLooks = [] {
    std::map<std::string, std::string> out;
    const auto put = [&out](std::initializer_list<const char*> ids, const char* look) {
        for (const char* id : ids) out[id] = look;
    };
    put({"julia", "mandel", "kifs", "fractal_temple", "menger_core", "mandelbox_cathedral", "supershape"}, "FRACTALS");
    put({"tunnel", "rod_tunnel", "particle_tunnel", "fractal_dive", "fractal_tunnel", "outrun_drive", "vanishing",
         "crystal_canyon", "spectrum_terrain", "gyroid_cavern", "noneuclid", "morph_world"},
        "FLY_THROUGH");
    put({"starfield", "nebula", "solar", "chrome_nebula", "ringed_planet", "black_hole", "aurora", "aurora_ocean"}, "SPACE");
    put({"orb_lattice", "neon_tiles", "chroma_orb", "blacklight_bloom", "prism_rain", "glow_vortex", "neon_chrome_sea",
         "neon_city", "merkaba_grid", "bead_vortex"},
        "GLASS");
    put({"kaleido", "grid", "voronoi", "hexgrid", "spiral", "mandala_dome", "hex_hive", "spiral_eye"}, "PATTERNS");
    put({"bars", "ring", "scope", "liss", "waves"}, "CLASSIC");
    put({"plasma", "warp", "metaballs", "lava", "morphogen", "curl_bloom", "nectar_flow", "jelly_abyss", "ribbon_storm",
         "ripples", "pool"},
        "FLOWING");
    put({"winter"}, "PSYCHEDELIC");
    return out;
}();

void everyStyleKeepsItsOldLookGroup() {
    const std::set<std::string> names = {"FLOWING", "LIVING", "PSYCHEDELIC", "FRACTALS", "SPACE",
                                         "FLY_THROUGH", "GLASS", "PATTERNS", "CLASSIC"};
    std::set<std::string> used;
    for (int l = 0; l < catalogue::kLookCount; ++l) used.insert(catalogue::name(static_cast<catalogue::Look>(l)));
    check(used == names, "look names are the Styles page's groups");
    std::map<std::string, std::string> familyOf;
    for (const auto& family : catalogue::families()) {
        for (const char* id : family.ids) familyOf[id] = family.name;
    }
    int loose = 0;
    for (const auto& id : catalogue::allIds()) {
        const std::string got = catalogue::name(catalogue::lookOf(id));
        std::string want;
        if (const auto f = familyOf.find(id); f != familyOf.end()) {
            want = kOldFamilyLooks.at(f->second);
        } else if (catalogue::kindOf(id) != Kind::Shader) {
            want = kOldKindLooks.at(catalogue::kindOf(id));
        } else {
            ++loose;
            const auto tag = kOldLooseLooks.find(id);
            check(tag != kOldLooseLooks.end(), "loose style " + id + " has a look tag");
            if (tag != kOldLooseLooks.end()) want = tag->second;
        }
        check(names.count(got) == 1, id + " has a look group");
        check(got == want, "look of " + id + ": got " + got + ", the Kotlin tags gave " + want);
    }
    check(loose == 62 && kOldLooseLooks.size() == 62, "every loose style is tagged once");
    check(catalogue::lookOf("no_such_style") == catalogue::Look::Flowing, "an unknown id is silk, so flowing");
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
    int kinds = 0, families = 0, keys = 0, overrides = 0, pickers = 0;
    for (const auto& line : lines) {
        const auto f = split(line, '|');
        if (f[0] == "style") {
            styleIds.push_back(f[1]);
            check(f[2] == catalogue::name(catalogue::kindOf(f[1])), "kind row of " + f[1]);
            check(f.size() >= 4 && f[3] == catalogue::name(catalogue::lookOf(f[1])), "look field of " + f[1]);
            std::vector<std::string> perStyle;
            if (catalogue::isMarched(f[1])) perStyle.emplace_back("MARCH_DETAIL");
            if (catalogue::spinRate(f[1]) != 0.0f) perStyle.emplace_back("SPIN");
            if (catalogue::drawsFamilyForm(f[1])) perStyle.emplace_back("FAMILY_FORM");
            if (catalogue::hasFormulas(f[1])) perStyle.emplace_back("FORMULA");
            if (catalogue::drawsShapes(f[1])) perStyle.emplace_back("SHAPES");
            check(f.size() >= 4 && std::vector<std::string>(f.begin() + 4, f.end()) == perStyle, "per-style scopes of " + f[1]);
        } else if (f[0] == "kind") {
            ++kinds;
        } else if (f[0] == "family") {
            ++families;
            const size_t n = split(f[4], ',').size();
            check(n == 12 || n == 10, "family row lists its twelve or ten ids");
        } else if (f[0] == "picker") {
            const auto& p = catalogue::pickerFamilies()[static_cast<size_t>(pickers++)];
            check(f.size() == 3 && f[1] == p.name && split(f[2], ',') == p.ids, "picker row of " + f[1]);
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
    check(pickers == static_cast<int>(catalogue::pickerFamilies().size()), "a picker row per picker family");
    // 86 keys: the 80 of the old hand list plus the six held controls whose scope Customize kept to itself
    // (Fluid quality and its auto switch, Progression, the two fluid layers, Water ripples on) until the
    // params schema's meta rows carried every control's scope (#1005 PR 4a); 95 with the dive's nine
    // tunnel controls (Customize › Dive), all on the endless zoom's scope; 85 once Pool replaced Water
    // (#1036): Water's seven controls and the ripple overlay's three are retired; 87 with Spin, which
    // took over Spin drift's field on the turning styles' own scope; 84 once the palette cleanup retired Colour map,
    // Palette 2 and Palette blend; 88 with Grow's four (Spawn, Fractal, Front layer, Back layer) on the family scope;
    // 89 with Formula on its own; 90 with Shapes on its own.
    check(kinds == catalogue::kKindCount && families == 19 && keys == 90 && overrides == 8, "row counts");
}

// The Styles page's dropdown: one flat level, every style once, in the order the stage swipes.
void pickerFamiliesAreFlatAndCoverEveryStyleOnce() {
    const auto& picker = catalogue::pickerFamilies();
    std::vector<std::string> ids;
    std::set<std::string> names;
    for (const auto& p : picker) {
        check(!p.ids.empty(), "picker family " + p.name + " has styles");
        check(names.insert(p.name).second, "picker family " + p.name + " is listed once");
        ids.insert(ids.end(), p.ids.begin(), p.ids.end());
    }
    check(ids == catalogue::allIds(), "picker families list every style once, in picker order");
    std::map<std::string, std::vector<std::string>> byName;
    for (const auto& p : picker) byName[p.name] = p.ids;
    for (const auto& family : catalogue::families()) {
        check(byName[family.name] == std::vector<std::string>(family.ids.begin(), family.ids.end()),
              "shader family " + std::string(family.name) + " is one picker family");
    }
    for (const auto& id : byName["shaders"]) {
        check(catalogue::kindOf(id) == Kind::Shader, id + " in shaders is a shader style");
        for (const auto& family : catalogue::families()) {
            check(std::find(family.ids.begin(), family.ids.end(), id) == family.ids.end(), id + " in shaders has no family");
        }
    }
    check(byName["shaders"].size() > 1 && std::count(byName["shaders"].begin(), byName["shaders"].end(), "neon_chrome_sea") == 1,
          "loose shader styles, the neon set among them, are in shaders");
    std::vector<std::string> fluid = styles::fluidIds();
    fluid.emplace_back("curlflow");
    check(byName["fluid"] == fluid, "Fluid and Curl Flow are one picker family");
    check(byName["milkdrop"] == std::vector<std::string>{"milkdrop"}, "MilkDrop is its own picker family");
}

// What the shell asks through core/api: every answer has to be the tables', in allIds() order.
void theShellsQuestionsAgreeWithTheTables() {
    using catalogue::Trait;
    const auto& all = catalogue::allIds();
    const auto ids = [](Trait trait) { return split(catalogue::idsText(trait), '\n'); };
    check(ids(Trait::Any) == all, "every style, in order");
    check(std::set<std::string>(all.begin(), all.end()).size() == all.size(), "no style is listed twice");

    std::vector<std::string> shader, follower, autoDraw, fluid, cymatics;
    std::set<std::string> openers;
    for (const auto& family : catalogue::families()) openers.insert(family.opener());
    for (const auto& id : all) {
        const Kind kind = catalogue::kindOf(id);
        bool inFamily = false;
        for (const auto& family : catalogue::families()) {
            inFamily = inFamily || std::find(family.ids.begin(), family.ids.end(), id) != family.ids.end();
        }
        if (kind == Kind::Shader) shader.push_back(id);
        if (inFamily && openers.count(id) == 0) follower.push_back(id);
        if (kind == Kind::Shader || kind == Kind::Silk || kind == Kind::Life || kind == Kind::Mycelium || kind == Kind::Acid) {
            autoDraw.push_back(id);
        }
        if (kind == Kind::Fluid) fluid.push_back(id);
        if (kind == Kind::Cymatics) cymatics.push_back(id);
    }
    check(ids(Trait::Shader) == shader, "the shader styles");
    check(ids(Trait::FamilyFollower) == follower, "every family style but the openers");
    check(!follower.empty() && follower.size() < shader.size(), "followers are some of the shader styles");
    for (const auto& opener : openers) check(!catalogue::has(Trait::FamilyFollower, opener), opener + " opens its family");
    check(ids(Trait::AutoDraw) == autoDraw, "what Auto draws from");
    check(!catalogue::has(Trait::AutoDraw, "milkdrop") && !catalogue::has(Trait::AutoDraw, "fluid"),
          "Auto draws no MilkDrop or Fluid");
    check(ids(Trait::Fluid) == fluid && fluid == styles::fluidIds(), "the Fluid looks");
    check(!catalogue::has(Trait::Fluid, "curlflow"), "Curl Flow is not a Fluid look");
    check(ids(Trait::Cymatics) == cymatics && cymatics == styles::cymaticsIds(), "the cymatics styles");
    for (int t = 0; t < catalogue::kTraitCount; ++t) {
        check(!catalogue::has(static_cast<Trait>(t), "no_such_style"), "an unknown id has no trait");
    }

    std::vector<std::string> names;
    for (const auto& picker : catalogue::pickerFamilies()) {
        names.push_back(picker.name);
        check(split(catalogue::pickerIdsText(picker.name), '\n') == picker.ids, "picker ids of " + picker.name);
    }
    check(split(catalogue::pickerNamesText(), '\n') == names, "picker names");
    check(catalogue::pickerIdsText("no_such_family").empty(), "an unknown family has no ids");

    for (const auto& id : all) {
        const Kind kind = catalogue::kindOf(id);
        const std::string want = kind == Kind::Fluid ? "fluid" : kind == Kind::Cymatics ? "cymatics" : id;
        check(catalogue::presetStyle(id) == want, "saved looks of " + id + " are filed under " + want);
    }
    check(catalogue::presetStyle("no_such_style") == "no_such_style", "an unknown id is its own shelf");

    // The table's override rows are every held control, so each answer is checked against them.
    std::map<std::string, std::string> held;
    for (const auto& line : split(catalogue::text(), '\n')) {
        const auto f = split(line, '|');
        if (f.size() == 4 && f[0] == "override") {
            check(f[2] == "Geometry", "only Geometry is held: " + line);
            held[f[1]] = f[3];
        }
    }
    check(held.count("kundt_tube") == 1 && held["kundt_tube"] == "1", "the tube is held to the second plate geometry");
    for (const auto& id : all) {
        const auto value = catalogue::overrideOf(id, "Geometry");
        check(value.has_value() == (held.count(id) == 1), "whether " + id + " holds Geometry");
        if (value && held.count(id) == 1) check(num(*value) == held[id], "the Geometry " + id + " holds");
        check(!catalogue::overrideOf(id, "Speed"), id + " holds no other control");
    }
    check(!catalogue::overrideOf("no_such_style", "Geometry"), "an unknown id holds nothing");

    check(catalogue::valueOf("fluidTiers") == static_cast<float>(fluid::quality::kTierCount), "the value fluidTiers");
    check(catalogue::valueOf("touchPoints") == static_cast<float>(TouchField::kMaxPoints), "the value touchPoints");
    check(catalogue::valueOf("touchRotationPerDegree") == catalogue::kTouchRotationPerDegree, "the value touchRotationPerDegree");
    check(!catalogue::valueOf("") && !catalogue::valueOf("no_such_value"), "any other name is no value");

    for (int s = 0; s < catalogue::kScopeCount; ++s) {
        const auto scope = static_cast<Scope>(s);
        check(catalogue::scopeNamed(catalogue::name(scope)) == scope, std::string("scope by name: ") + catalogue::name(scope));
    }
    check(!catalogue::scopeNamed("") && !catalogue::scopeNamed("universal"), "any other text names no scope");
    // The scopes a screen names outright (render/scene/StyleCatalogue.kt): a rename here would hide nothing there.
    const std::pair<const char*, Scope> shell[] = {
        {"UNIVERSAL", Scope::Universal},
        {"BAND_GAINS", Scope::BandGains},
        {"SHADER_LOOK", Scope::ShaderLook},
        {"MARCH_DETAIL", Scope::MarchDetail},
        {"ENDLESS_ZOOM", Scope::EndlessZoom},
        {"FLUID_SIM", Scope::FluidSim},
        {"EMITTERS", Scope::Emitters},
        {"JOURNEY", Scope::Journey},
        {"CYMATICS", Scope::Cymatics},
    };
    for (const auto& [text, scope] : shell) {
        check(catalogue::scopeNamed(text) == scope, std::string("the shell's name for a scope: ") + text);
    }
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

// The engine numbers the shell sizes things by: the touch field's finger slots are what the gestures and the
// wallpaper collect, so they come from the field itself.
void valuesTheShellReads() {
    const std::string& text = catalogue::text();
    check(text.find("\nvalue|touchPoints|" + std::to_string(TouchField::kMaxPoints) + "\n") != std::string::npos,
          "the touch field's slots are a catalogue value");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << catalogue::text();
        return 0;
    }
    everyOldStyleKeepsItsNumbersAndKind();
    familiesCarryTheirOwnCountAndAnOpener();
    spinRateAndShaderStyleAgree();
    marchedStylesMatchTheOldSet();
    paramScopesMatchTheOldParamScope();
    everyStyleKeepsItsOldLookGroup();
    pickerFamiliesAreFlatAndCoverEveryStyleOnce();
    theShellsQuestionsAgreeWithTheTables();
    textRoundTrips();
    valuesTheShellReads();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/catalogue.txt");
    if (failures == 0) std::printf("catalogue_test: all checks passed\n");
    return failures;
}
