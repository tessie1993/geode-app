#include "viz/Catalogue.hpp"

#include <algorithm>
#include <cstdio>

#include "viz/Params.hpp"
#include "viz/TouchField.hpp"
#include "viz/fluid/FluidQuality.hpp"
#include "viz/scenes/CymaticsMath.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz::catalogue {

namespace {

// The loose fragment styles (ShaderScene compiles shaders/<id>_frag.glsl), in picker order. The
// families' styles follow them.
constexpr std::array<const char*, 62> kLooseShaders = {
    "julia", "tunnel", "mandel", "kaleido", "plasma", "bars", "ring", "scope", "liss", "warp", "grid", "voronoi",
    "metaballs", "ripples", "starfield", "waves", "hexgrid", "spiral", "aurora", "solar", "winter", "lava",
    // Built on the shared GLSL libraries (lib_scene_uniforms, lib_scene_grade, lib_sdf3, lib_touch).
    "vanishing", "morphogen", "nebula", "noneuclid", "kifs", "orb_lattice", "rod_tunnel", "neon_tiles",
    // The three reference looks: 2D tunnel and sphere mappings, no raymarch.
    "chroma_orb", "mandala_dome", "bead_vortex",
    // The four reference-clip looks with the curl-advected dye of lib_scene_motion; 2D, no raymarch.
    "merkaba_grid", "spiral_eye", "blacklight_bloom", "fractal_temple",
    // The fluid in 3D: both march.
    "curl_bloom", "nectar_flow",
    // Fly-throughs on the shared camera rig (lib_camera); all march.
    "crystal_canyon", "neon_city", "particle_tunnel", "chrome_nebula", "fractal_dive", "aurora_ocean",
    // The second rig set: kick, snare, hats, onsets, drops and pitch each move a different property.
    "spectrum_terrain", "outrun_drive", "menger_core", "gyroid_cavern", "ringed_planet", "jelly_abyss", "ribbon_storm",
    "prism_rain",
    // Two looks restored from branches that never merged (#14, #7), rewritten on the sound engine.
    "supershape", "fractal_tunnel",
    // The neon set, written from scratch on lib_neon: a 2D spiral web,
    // then four marches (a chrome sea, a lensed singularity, a hex-pillar tunnel, a Mandelbox nave).
    "glow_vortex", "neon_chrome_sea", "black_hole", "hex_hive", "mandelbox_cathedral",
    // Morph World (kMorphWorld): one morphing environment on lib_world, its World Genome eased by core/viz/world.
    "morph_world",
    // Pool (kPool): a pool's floor under moving water, its surface stepped by core/viz/pool.
    "pool",
};

// The loose styles that raymarch, and so spend the Detail budget. Per STYLE rather than per kind:
// Outrun Drive and Vanishing are both shader styles and only one has a march to spend steps on.
constexpr std::array<const char*, 58> kLooseMarched = {
    "vanishing", "morphogen", "nebula", "noneuclid", "kifs", "rod_tunnel",
    // Curl Bloom marches a distance field, Nectar Flow a volume; Detail is the budget for both.
    "curl_bloom", "nectar_flow", "crystal_canyon", "neon_city", "particle_tunnel", "chrome_nebula", "fractal_dive",
    "aurora_ocean", "spectrum_terrain", "menger_core", "gyroid_cavern", "jelly_abyss", "prism_rain",
    "supershape", "fractal_tunnel",
    // The neon set's marches (Glow Vortex, its flat one, is in the 3D wave below).
    "neon_chrome_sea", "black_hole", "hex_hive", "mandelbox_cathedral",
    // The flat styles rebuilt in 3D on the camera rig, one ticket each (the style3d wave): marched from
    // here so the parallel rewrites never touch this list.
    "julia", "tunnel", "mandel", "kaleido", "plasma", "bars", "ring", "scope", "liss", "warp", "grid", "voronoi",
    "metaballs", "ripples", "starfield", "waves", "hexgrid", "spiral", "aurora", "solar", "winter", "lava",
    "orb_lattice", "neon_tiles", "chroma_orb", "mandala_dome", "bead_vortex", "merkaba_grid", "spiral_eye",
    "blacklight_bloom", "fractal_temple", "glow_vortex",
    // Morph World marches up to three ways (surface, glow-through, density), all on the Detail budget.
    "morph_world",
};

// Marching and flying the camera rig (lib_camera's uCamBasis) mostly go together. These five march
// from a view of their own, so the Camera framing and feel controls have nothing to move.
constexpr std::array<const char*, 5> kMarchedOffRig = {"vanishing", "morphogen", "noneuclid", "curl_bloom", "nectar_flow"};
// And these three fly the rig through scenes they draw without a march.
constexpr std::array<const char*, 3> kRigUnmarched = {"outrun_drive", "ringed_planet", "ribbon_storm"};

// The look group of each loose shader style (the owner's tags, first drafted in the Styles page's
// StyleLookTags). Every loose style is listed; catalogue_test fails a loose style missing here, so a
// new one is placed on purpose. Winter had no tag there and showed under Psychedelic; it keeps that.
struct LooseLook {
    const char* id;
    Look look;
};
constexpr std::array<LooseLook, 62> kLooseLooks = {{
    {"julia", Look::Fractals},           {"tunnel", Look::FlyThrough},       {"mandel", Look::Fractals},
    {"kaleido", Look::Patterns},         {"plasma", Look::Flowing},          {"bars", Look::Classic},
    {"ring", Look::Classic},             {"scope", Look::Classic},           {"liss", Look::Classic},
    {"warp", Look::Flowing},             {"grid", Look::Patterns},           {"voronoi", Look::Patterns},
    {"metaballs", Look::Flowing},        {"ripples", Look::Flowing},         {"starfield", Look::Space},
    {"waves", Look::Classic},            {"hexgrid", Look::Patterns},        {"spiral", Look::Patterns},
    {"aurora", Look::Space},             {"solar", Look::Space},             {"winter", Look::Psychedelic},
    {"lava", Look::Flowing},             {"vanishing", Look::FlyThrough},    {"morphogen", Look::Flowing},
    {"nebula", Look::Space},             {"noneuclid", Look::FlyThrough},    {"kifs", Look::Fractals},
    {"orb_lattice", Look::Glass},        {"rod_tunnel", Look::FlyThrough},   {"neon_tiles", Look::Glass},
    {"chroma_orb", Look::Glass},         {"mandala_dome", Look::Patterns},   {"bead_vortex", Look::Glass},
    {"merkaba_grid", Look::Glass},       {"spiral_eye", Look::Patterns},     {"blacklight_bloom", Look::Glass},
    {"fractal_temple", Look::Fractals},  {"curl_bloom", Look::Flowing},      {"nectar_flow", Look::Flowing},
    {"crystal_canyon", Look::FlyThrough}, {"neon_city", Look::Glass},        {"particle_tunnel", Look::FlyThrough},
    {"chrome_nebula", Look::Space},      {"fractal_dive", Look::FlyThrough}, {"aurora_ocean", Look::Space},
    {"spectrum_terrain", Look::FlyThrough}, {"outrun_drive", Look::FlyThrough}, {"menger_core", Look::Fractals},
    {"gyroid_cavern", Look::FlyThrough}, {"ringed_planet", Look::Space},     {"jelly_abyss", Look::Flowing},
    {"ribbon_storm", Look::Flowing},     {"prism_rain", Look::Glass},        {"supershape", Look::Fractals},
    {"fractal_tunnel", Look::FlyThrough}, {"glow_vortex", Look::Glass},      {"neon_chrome_sea", Look::Glass},
    {"black_hole", Look::Space},         {"hex_hive", Look::Patterns},       {"mandelbox_cathedral", Look::Fractals},
    {"morph_world", Look::FlyThrough}, {"pool", Look::Flowing},
}};

// Each family lists its own styles, so it carries its own count; the opener is first.
// Fractal trees: branching folded distance fields, flown through or orbited.
constexpr const char* kArborIds[] = {
    "arbor_grove", "arbor_willow", "arbor_coral", "arbor_frost", "arbor_bolt", "arbor_roots", "arbor_bonsai", "arbor_canopy",
    "arbor_rosette", "arbor_spire", "arbor_synapse", "arbor_sakura"};
// Ray-marched power-N Mandelbulbs and their Julia relatives.
constexpr const char* kBulbIds[] = {
    "bulb_classic", "bulb_julia", "bulb_slice", "bulb_lotus", "bulb_ink", "bulb_glow", "bulb_cavern", "bulb_shell",
    "bulb_prism", "bulb_contour", "bulb_chrome", "bulb_hybrid"};
// Polar petal fields and mandalas that bloom out of a glowing core.
constexpr const char* kChrysanthemumIds[] = {
    "chrys_bloom", "chrys_phyllo", "chrys_dahlia", "chrys_lotus", "chrys_meadow", "chrys_corolla", "chrys_glass", "chrys_ink",
    "chrys_neon", "chrys_fall", "chrys_kaleido", "chrys_tunnel"};
// Nested domain warps, often folded into a kaleidoscope.
constexpr const char* kLucidIds[] = {
    "lucid_weave", "lucid_mandala", "lucid_spiral", "lucid_tunnel", "lucid_disc", "lucid_hive", "lucid_ink", "lucid_mirror",
    "lucid_bulge", "lucid_petal", "lucid_lattice", "lucid_vortex"};
// Fly-through dives along five camera paths.
constexpr const char* kDiveIds[] = {
    "dive_lattice", "dive_jewel", "dive_mushroom", "dive_gyro", "dive_mandala", "dive_serpent", "dive_helix",
    "dive_accordion", "dive_orbit", "dive_neon", "dive_cycle", "dive_show"};
// Ray-traced glass: refraction, dispersion, thin films and caustic light.
constexpr const char* kGlassIds[] = {
    "glass_orb", "glass_pool", "glass_prism", "glass_lens", "glass_drops", "glass_bubbles", "glass_marble", "glass_caustic",
    "glass_cubes", "glass_gem", "glass_ice", "glass_knot"};
// Galaxies and gas clouds.
constexpr const char* kNebulaIds[] = {
    "nebula_dust", "nebula_spiral", "nebula_ring", "nebula_veil", "nebula_pulsar", "nebula_cluster", "nebula_nova",
    "nebula_horizon", "nebula_pillars", "nebula_flight", "nebula_storm", "nebula_web"};
// Ray-marched heightfield landscapes.
constexpr const char* kTerrainIds[] = {
    "terra_ridge", "terra_dunes", "terra_canyon", "terra_glacier", "terra_isles", "terra_mesa", "terra_volcano",
    "terra_terraces", "terra_moon", "terra_night", "terra_topo", "terra_wire"};
// Truchet tilings whose paths rewire on the beat.
constexpr const char* kTruchetIds[] = {
    "truchet_arcs", "truchet_hex", "truchet_weave", "truchet_maze", "truchet_tri", "truchet_quad", "truchet_tubes",
    "truchet_neon", "truchet_flow", "truchet_knot", "truchet_tunnel", "truchet_relief"};
// Falls and orbits through a 3D Apollonian sphere packing (after PR #7), one material each.
constexpr const char* kFoamIds[] = {
    "foam_fall", "foam_neon", "foam_soap", "foam_chrome", "foam_ember", "foam_ice", "foam_orb",
    "foam_kaleido", "foam_ink", "foam_coral", "foam_ghost", "foam_show"};
// Fractal-flame wisps: thin luminous veils and filaments gathered along the ray, on black.
constexpr const char* kFlameIds[] = {
    "flame_veil", "flame_spiral", "flame_orb", "flame_star", "flame_lotus",
    "flame_helix", "flame_knot", "flame_web", "flame_aurora", "flame_nova"};
// Iridescent glass and liquid: clear shells seen through each other, thin-film colour at the rims.
constexpr const char* kOpalIds[] = {
    "opal_bubbles", "opal_nautilus", "opal_melt", "opal_domes", "opal_pearls",
    "opal_lava", "opal_ribbon", "opal_cells", "opal_rings", "opal_drop"};
// Organic tunnels: sinew, bone and carved cave walls whose relief glows under blacklight.
constexpr const char* kHollowIds[] = {
    "hollow_sinew", "hollow_cave", "hollow_ribs", "hollow_coral", "hollow_roots",
    "hollow_hive", "hollow_throat", "hollow_neuron", "hollow_bone", "hollow_shell"};

constexpr Family kFamilies[] = {
    {"arbor", "arbor_", "lib_arbor", Look::Fractals, kArborIds},
    {"bulb", "bulb_", "lib_bulb", Look::Fractals, kBulbIds},
    {"chrysanthemum", "chrys_", "lib_chrys", Look::Psychedelic, kChrysanthemumIds},
    {"lucid", "lucid_", "lib_lucid", Look::Psychedelic, kLucidIds},
    {"dive", "dive_", "lib_dive", Look::FlyThrough, kDiveIds},
    {"glass", "glass_", "lib_glass", Look::Glass, kGlassIds},
    {"nebula", "nebula_", "lib_nebula", Look::Space, kNebulaIds},
    {"terrain", "terra_", "lib_terra", Look::FlyThrough, kTerrainIds},
    {"truchet", "truchet_", "lib_truchet", Look::Patterns, kTruchetIds},
    {"foam", "foam_", "lib_foam", Look::Fractals, kFoamIds},
    {"flame", "flame_", "lib_flame", Look::Psychedelic, kFlameIds},
    {"opal", "opal_", "lib_opal", Look::Glass, kOpalIds},
    {"hollow", "hollow_", "lib_hollow", Look::FlyThrough, kHollowIds},
};

// The family styles that march.
constexpr std::array kFamilyMarched = {
    "arbor_willow", "arbor_coral", "arbor_bonsai", "arbor_canopy", "arbor_spire", "arbor_synapse",
    "bulb_classic", "bulb_julia", "bulb_slice", "bulb_lotus", "bulb_ink", "bulb_glow", "bulb_cavern", "bulb_shell",
    "bulb_prism", "bulb_contour", "bulb_chrome", "bulb_hybrid",
    "chrys_corolla",
    "dive_lattice", "dive_jewel", "dive_mushroom", "dive_gyro", "dive_mandala", "dive_serpent", "dive_helix",
    "dive_accordion", "dive_orbit", "dive_neon", "dive_cycle", "dive_show",
    "glass_cubes", "glass_gem", "glass_ice", "glass_knot",
    "nebula_pillars", "nebula_flight", "nebula_storm", "nebula_web",
    "terra_ridge", "terra_dunes", "terra_canyon", "terra_glacier", "terra_isles", "terra_mesa", "terra_volcano",
    "terra_terraces", "terra_moon", "terra_night", "terra_wire",
    "truchet_relief",
    "foam_fall", "foam_neon", "foam_soap", "foam_chrome", "foam_ember", "foam_ice",
    "foam_orb", "foam_kaleido", "foam_ink", "foam_coral", "foam_ghost", "foam_show",
    // The flat family styles rebuilt in 3D, one ticket each (the style3d wave, part two).
    "arbor_grove", "arbor_frost", "arbor_bolt", "arbor_roots", "arbor_rosette", "arbor_sakura", "chrys_bloom",
    "chrys_phyllo", "chrys_dahlia", "chrys_lotus", "chrys_meadow", "chrys_glass", "chrys_ink", "chrys_neon",
    "chrys_fall", "chrys_kaleido", "chrys_tunnel", "lucid_weave", "lucid_mandala", "lucid_spiral", "lucid_tunnel",
    "lucid_disc", "lucid_hive", "lucid_ink", "lucid_mirror", "lucid_bulge", "lucid_petal", "lucid_lattice",
    "lucid_vortex", "glass_orb", "glass_pool", "glass_prism", "glass_lens", "glass_drops", "glass_bubbles",
    "glass_marble", "glass_caustic", "nebula_dust", "nebula_spiral", "nebula_ring", "nebula_veil", "nebula_pulsar",
    "nebula_cluster", "nebula_nova", "nebula_horizon", "terra_topo", "truchet_arcs", "truchet_hex", "truchet_weave",
    "truchet_maze", "truchet_tri", "truchet_quad", "truchet_tubes", "truchet_neon", "truchet_flow", "truchet_knot",
    "truchet_tunnel",
    // Fractal Flame, Opal and Hollows: every style flies the rig and marches.
    "flame_veil", "flame_spiral", "flame_orb", "flame_star", "flame_lotus", "flame_helix",
    "flame_knot", "flame_web", "flame_aurora", "flame_nova", "opal_bubbles", "opal_nautilus",
    "opal_melt", "opal_domes", "opal_pearls", "opal_lava", "opal_ribbon", "opal_cells",
    "opal_rings", "opal_drop", "hollow_sinew", "hollow_cave", "hollow_ribs", "hollow_coral",
    "hollow_roots", "hollow_hive", "hollow_throat", "hollow_neuron", "hollow_bone", "hollow_shell",
};

// The styles whose look is a turning one (kaleidoscopes, mandalas, spirals, vortices, tunnels),
// each with its own rate and direction so no two turn alike: a vortex winds fast, a dome barely
// creeps, tunnels counter. The user's Spin control scales the rate, and at 0 these hold still too.
// Every other style holds still unless the user turns it.
struct Spin {
    const char* id;
    float rate;
};
constexpr std::array<Spin, 22> kSpinning = {{
    {"kaleido", 1.0f},        {"mandala_dome", 0.45f},  {"orb_lattice", 0.7f},   {"chroma_orb", -0.6f},
    {"kifs", 0.8f},           {"spiral", 1.2f},         {"spiral_eye", -0.9f},   {"bead_vortex", 1.4f},
    {"merkaba_grid", 0.4f},   {"tunnel", -0.5f},        {"rod_tunnel", 0.6f},    {"particle_tunnel", -0.8f},
    {"lucid_mandala", 0.6f},  {"lucid_mirror", -0.7f},  {"lucid_spiral", 1.1f},  {"lucid_vortex", -1.3f},
    {"lucid_tunnel", 0.5f},   {"lucid_disc", 0.35f},    {"chrys_kaleido", -0.8f}, {"chrys_tunnel", 0.6f},
    // The neon set's two turning looks: the vortex winds against its own arms, the hive's bore rolls slowly.
    // The sea, the hole and the nave keep still: a horizon, a disc plane or a nave axis only tips over in a roll.
    {"glow_vortex", -1.1f},   {"hex_hive", 0.3f},
}};

constexpr const char* kMilkdrop = "milkdrop";
constexpr const char* kCurlFlow = "curlflow";
// The Customize control a cymatics style can hold (its ParamKeys string).
constexpr const char* kGeometryKey = "Geometry";

constexpr const char* kKindNames[kKindCount] = {"SHADER", "SILK",     "LIFE",  "MYCELIUM", "ACID",
                                                "MILKDROP", "FLUID", "CURL_FLOW", "CYMATICS"};
constexpr const char* kScopeNames[kScopeCount] = {
    "UNIVERSAL",    "SCENE_CLOCK",     "AUDIO_DRIVE", "BASS_BAND", "MID_BAND",     "TREBLE_BAND", "BAND_GAINS", "SHADER_LOOK",
    "MARCH_DETAIL", "ENDLESS_ZOOM",    "TURBULENCE",  "DYE_DENSITY", "TRAIL_TOGGLE", "TRAIL_LENGTH", "TRAIL_ECHO",
    "PARTICLE_SPRITE", "MILKDROP",     "FLUID_SIM",   "EMITTERS",  "JOURNEY",      "CYMATICS",
    "FLOW_FIELD_SIM",  "SPIN",
};

constexpr const char* kLookNames[kLookCount] = {"FLOWING", "LIVING", "PSYCHEDELIC", "FRACTALS", "SPACE",
                                                "FLY_THROUGH", "GLASS", "PATTERNS", "CLASSIC"};

// A dedicated renderer's styles all share one group; shader styles go by family or by their own tag.
Look kindLook(Kind k) {
    switch (k) {
        case Kind::Silk:
        case Kind::Fluid:
        case Kind::CurlFlow: return Look::Flowing;
        case Kind::Life:
        case Kind::Mycelium: return Look::Living;
        case Kind::Acid: return Look::Psychedelic;
        case Kind::Cymatics: return Look::Patterns;
        case Kind::Milkdrop: return Look::Classic;
        case Kind::Shader: break;
    }
    return Look::Psychedelic;
}

template <typename Table>
bool listed(const Table& table, const std::string& id) {
    for (const char* known : table) {
        if (id == known) return true;
    }
    return false;
}

bool familyStyle(const std::string& id) {
    for (const auto& family : kFamilies) {
        if (listed(family.ids, id)) return true;
    }
    return false;
}

// A simulation presented in 3D (core/viz/present/FieldPresent): its present is drawn by a ShaderScene on the
// shared block, so it reads the shader look, the bands and Turbulence as a shader style does, and marches.
bool presentsIn3d(Kind k) { return k == Kind::Life; }

// Per-band gain reaches only a style that reads that band's envelope; MilkDrop takes raw PCM.
bool readsBass(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Mycelium || k == Kind::Acid || k == Kind::Fluid || presentsIn3d(k);
}
bool readsMid(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Acid || k == Kind::Fluid || k == Kind::CurlFlow || presentsIn3d(k);
}
bool readsTreble(Kind k) { return k != Kind::Milkdrop; }

// Whether a scope reaches every style of a kind. MarchDetail and Spin are per style, so no kind has them.
bool reachesKind(Scope s, Kind k) {
    switch (s) {
        case Scope::Universal: return true;
        case Scope::SceneClock:
        case Scope::AudioDrive: return k != Kind::Milkdrop;
        case Scope::BassBand: return readsBass(k);
        case Scope::MidBand: return readsMid(k);
        case Scope::TrebleBand: return readsTreble(k);
        case Scope::BandGains: return readsBass(k) || readsMid(k) || readsTreble(k);
        case Scope::ShaderLook: return k == Kind::Shader || presentsIn3d(k);
        case Scope::MarchDetail: return false;
        case Scope::EndlessZoom: return true;
        case Scope::Turbulence:
            return k == Kind::Shader || k == Kind::Mycelium || k == Kind::Acid || k == Kind::CurlFlow || presentsIn3d(k);
        case Scope::DyeDensity: return k == Kind::Fluid;
        // A shader style's trails are the feedback pass's sustain, echo zoom and warp (EndlessZoom.hpp).
        case Scope::TrailToggle:
        case Scope::TrailLength: return k == Kind::Shader || k == Kind::Silk || k == Kind::CurlFlow;
        case Scope::TrailEcho: return k == Kind::Shader || k == Kind::CurlFlow;
        case Scope::ParticleSprite: return k == Kind::Fluid || k == Kind::CurlFlow;
        case Scope::Milkdrop: return k == Kind::Milkdrop;
        case Scope::FluidSim: return k == Kind::Fluid;
        case Scope::Emitters: return k == Kind::Fluid;
        case Scope::Journey: return k == Kind::Fluid || k == Kind::CurlFlow;
        case Scope::Cymatics: return k == Kind::Cymatics;
        case Scope::FlowFieldSim: return k != Kind::Fluid;
        case Scope::Spin: return false;
    }
    return false;
}

// The Styles page family of a style outside the shader families: its renderer's.
const char* pickerName(Kind k) {
    switch (k) {
        case Kind::Shader: return "shaders";
        case Kind::Silk: return "silk";
        case Kind::Life: return "life";
        case Kind::Mycelium: return "mycelium";
        case Kind::Acid: return "morphing";
        case Kind::Milkdrop: return "milkdrop";
        case Kind::Fluid:
        case Kind::CurlFlow: return "fluid";
        case Kind::Cymatics: return "cymatics";
    }
    return "shaders";
}

void append(std::vector<std::string>& out, const std::vector<std::string>& ids) { out.insert(out.end(), ids.begin(), ids.end()); }

std::string number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

std::string build() {
    std::string out = "catalogue|" + std::to_string(kTextVersion) + "\n";
    for (int k = 0; k < kKindCount; ++k) {
        out += "kind|";
        out += kKindNames[k];
        out += '|';
        bool first = true;
        for (int s = 0; s < kScopeCount; ++s) {
            if (!reachesKind(static_cast<Scope>(s), static_cast<Kind>(k))) continue;
            if (!first) out += ',';
            out += kScopeNames[s];
            first = false;
        }
        out += '\n';
    }
    for (const auto& id : allIds()) {
        out += "style|" + id + "|" + name(kindOf(id)) + "|" + name(lookOf(id));
        if (isMarched(id)) out += "|MARCH_DETAIL";
        if (spinRate(id) != 0.0f) out += "|SPIN";
        out += '\n';
    }
    for (const auto& r : kRetired) out += std::string("retired|") + r.id + "|" + r.now + "\n";
    for (const auto& family : kFamilies) {
        out += std::string("family|") + family.name + "|" + family.prefix + "|" + family.library + "|";
        for (size_t i = 0; i < family.ids.size(); ++i) {
            if (i > 0) out += ',';
            out += family.ids[i];
        }
        out += '\n';
    }
    for (const auto& picker : pickerFamilies()) {
        out += "picker|" + picker.name + "|";
        for (size_t i = 0; i < picker.ids.size(); ++i) {
            if (i > 0) out += ',';
            out += picker.ids[i];
        }
        out += '\n';
    }
    // A cymatics style that draws on one plate geometry overrides the user's Geometry control.
    for (const auto& id : styles::cymaticsIds()) {
        const int geometry = styles::cymatics(id)->geometryOverride;
        if (geometry >= 0) out += "override|" + id + "|" + kGeometryKey + "|" + std::to_string(geometry) + "\n";
    }
    // Every control that is NOT universal, by its key, from the params schema's meta rows in wire order
    // (a control of several fields once): the composite pass applies the rest to the finished frame.
    std::vector<std::string> keyed;
    for (const ParamMeta& meta : SceneParams::metaTable()) {
        if (!meta.live || meta.label[0] == '\0' || meta.scope == Scope::Universal) continue;
        if (std::find(keyed.begin(), keyed.end(), meta.label) != keyed.end()) continue;
        keyed.emplace_back(meta.label);
        out += std::string("key|") + meta.label + "|" + name(meta.scope) + "\n";
    }
    const auto range = [&out](const char* key, float lo, float hi) {
        out += std::string("range|") + key + "|" + number(lo) + "|" + number(hi) + "\n";
    };
    range("Detail", kMarchDetail.min, kMarchDetail.max);
    range("Fundamental (Hz)", cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz);
    range("Standing waves", 1.0f, static_cast<float>(cymatics::kMaxRenderedModes));
    range("Zoom", kTouchZoom.min, kTouchZoom.max);
    range("Rotation", kTouchRotation.min, kTouchRotation.max);
    // The Fluid tab's quality chips are labelled by the shell; this says how many tiers they index.
    out += "value|fluidTiers|" + std::to_string(fluid::quality::kTierCount) + "\n";
    out += "value|touchRotationPerDegree|" + number(kTouchRotationPerDegree) + "\n";
    // The fingers the touch field takes (geode_viz_set_touch); the shell collects no more than this.
    out += "value|touchPoints|" + std::to_string(TouchField::kMaxPoints) + "\n";
    return out;
}

}  // namespace

const std::vector<std::string>& allIds() {
    static const std::vector<std::string> kIds = [] {
        std::vector<std::string> out;
        append(out, styles::silkIds());
        append(out, styles::lifeIds());
        append(out, styles::mycoIds());
        append(out, styles::acidIds());
        out.insert(out.end(), kLooseShaders.begin(), kLooseShaders.end());
        for (const auto& family : kFamilies) out.insert(out.end(), family.ids.begin(), family.ids.end());
        out.emplace_back(kMilkdrop);
        append(out, styles::fluidIds());
        out.emplace_back(kCurlFlow);
        append(out, styles::cymaticsIds());
        return out;
    }();
    return kIds;
}

const std::vector<PickerFamily>& pickerFamilies() {
    static const std::vector<PickerFamily> kPicker = [] {
        std::vector<PickerFamily> out;
        for (const auto& id : allIds()) {
            std::string name = pickerName(kindOf(id));
            for (const auto& family : kFamilies) {
                if (listed(family.ids, id)) name = family.name;
            }
            auto it = std::find_if(out.begin(), out.end(), [&](const PickerFamily& p) { return p.name == name; });
            if (it == out.end()) it = out.insert(out.end(), PickerFamily{name, {}});
            it->ids.push_back(id);
        }
        return out;
    }();
    return kPicker;
}

const std::string& pickerNamesText() {
    static const std::string kNames = [] {
        std::string out;
        for (const auto& picker : pickerFamilies()) out += picker.name + "\n";
        return out;
    }();
    return kNames;
}

std::string pickerIdsText(const std::string& name) {
    std::string out;
    for (const auto& picker : pickerFamilies()) {
        if (picker.name != name) continue;
        for (const auto& id : picker.ids) out += id + "\n";
    }
    return out;
}

bool has(Trait trait, const std::string& id) {
    switch (trait) {
        case Trait::Any: return knows(id);
        case Trait::Shader: return isShaderStyle(id);
        case Trait::FamilyFollower:
            for (const auto& family : kFamilies) {
                if (listed(family.ids, id)) return id != family.opener();
            }
            return false;
        case Trait::AutoDraw:
            return isShaderStyle(id) || styles::silk(id) || styles::life(id) || styles::myco(id) || styles::acid(id);
        case Trait::Fluid: return styles::fluid(id) != nullptr;
        case Trait::Cymatics: return styles::cymatics(id) != nullptr;
    }
    return false;
}

const std::string& idsText(Trait trait) {
    static const std::array<std::string, kTraitCount> kTexts = [] {
        std::array<std::string, kTraitCount> out;
        for (int t = 0; t < kTraitCount; ++t) {
            for (const auto& id : allIds()) {
                if (has(static_cast<Trait>(t), id)) out[static_cast<size_t>(t)] += id + "\n";
            }
        }
        return out;
    }();
    return kTexts[static_cast<size_t>(trait)];
}

std::string presetStyle(const std::string& id) {
    if (styles::fluid(id)) return styles::fluidIds().front();
    if (styles::cymatics(id)) return styles::cymaticsIds().front();
    return id;
}

std::optional<float> overrideOf(const std::string& styleId, const std::string& paramKey) {
    if (paramKey != kGeometryKey) return std::nullopt;
    const auto* style = styles::cymatics(styleId);
    if (style == nullptr || style->geometryOverride < 0) return std::nullopt;
    return static_cast<float>(style->geometryOverride);
}

std::optional<float> valueOf(const std::string& name) {
    if (name == "fluidTiers") return static_cast<float>(fluid::quality::kTierCount);
    if (name == "touchRotationPerDegree") return kTouchRotationPerDegree;
    if (name == "touchPoints") return static_cast<float>(TouchField::kMaxPoints);
    return std::nullopt;
}

bool isShaderStyle(const std::string& id) { return listed(kLooseShaders, id) || familyStyle(id); }

bool knows(const std::string& id) {
    return isShaderStyle(id) || styles::cymatics(id) || styles::silk(id) || styles::life(id) || styles::acid(id) ||
           styles::myco(id) || styles::fluid(id) || id == kMilkdrop || id == kCurlFlow;
}

const std::string& resolveId(const std::string& id) {
    static const std::array<std::string, kRetired.size()> now = [] {
        std::array<std::string, kRetired.size()> out;
        for (size_t i = 0; i < kRetired.size(); ++i) out[i] = kRetired[i].now;
        return out;
    }();
    for (size_t i = 0; i < kRetired.size(); ++i) {
        if (id == kRetired[i].id) return now[i];
    }
    return id;
}

Kind kindOf(const std::string& id) {
    if (isShaderStyle(id)) return Kind::Shader;
    if (id == kMilkdrop) return Kind::Milkdrop;
    if (styles::fluid(id)) return Kind::Fluid;
    if (id == kCurlFlow) return Kind::CurlFlow;
    if (styles::cymatics(id)) return Kind::Cymatics;
    if (styles::life(id)) return Kind::Life;
    if (styles::myco(id)) return Kind::Mycelium;
    if (styles::acid(id)) return Kind::Acid;
    return Kind::Silk;
}

bool isMarched(const std::string& id) {
    return listed(kLooseMarched, id) || listed(kFamilyMarched, id) || presentsIn3d(kindOf(id));
}

bool fliesCamera(const std::string& id) {
    return listed(kRigUnmarched, id) || (isMarched(id) && !listed(kMarchedOffRig, id));
}

float spinRate(const std::string& id) {
    for (const Spin& known : kSpinning) {
        if (id == known.id) return known.rate;
    }
    return 0.0f;
}

std::span<const Family> families() { return kFamilies; }

Look lookOf(const std::string& id) {
    for (const auto& family : kFamilies) {
        if (listed(family.ids, id)) return family.look;
    }
    const Kind kind = kindOf(id);
    if (kind != Kind::Shader) return kindLook(kind);
    for (const LooseLook& loose : kLooseLooks) {
        if (id == loose.id) return loose.look;
    }
    // A loose style nobody tagged still shows somewhere; catalogue_test fails it so it gets a tag.
    return Look::Psychedelic;
}

Scope scopeOf(const std::string& paramKey) {
    for (const ParamMeta& meta : SceneParams::metaTable()) {
        if (meta.live && paramKey == meta.label) return meta.scope;
    }
    return Scope::Universal;
}

bool reaches(Scope scope, const std::string& styleId) {
    if (scope == Scope::MarchDetail) return isMarched(styleId);
    if (scope == Scope::Spin) return spinRate(styleId) != 0.0f;
    return reachesKind(scope, kindOf(styleId));
}

const char* name(Kind kind) { return kKindNames[static_cast<int>(kind)]; }
const char* name(Scope scope) { return kScopeNames[static_cast<int>(scope)]; }
const char* name(Look look) { return kLookNames[static_cast<int>(look)]; }

std::optional<Scope> scopeNamed(const std::string& name) {
    for (int s = 0; s < kScopeCount; ++s) {
        if (name == kScopeNames[s]) return static_cast<Scope>(s);
    }
    return std::nullopt;
}

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::catalogue
