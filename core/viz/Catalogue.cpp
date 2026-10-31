#include "viz/Catalogue.hpp"

#include <cstdio>

#include "viz/fluid/FluidQuality.hpp"
#include "viz/scenes/CymaticsMath.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz::catalogue {

namespace {

// The loose fragment styles (ShaderScene compiles shaders/<id>_frag.glsl), in picker order. The
// families' styles follow them.
constexpr std::array<const char*, 53> kLooseShaders = {
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
};

// The loose styles that raymarch, and so spend the Detail budget. Per STYLE rather than per kind:
// Plasma and Vanishing are both shader styles and only one has a march to spend steps on.
constexpr std::array<const char*, 19> kLooseMarched = {
    "vanishing", "morphogen", "nebula", "noneuclid", "kifs", "rod_tunnel",
    // Curl Bloom marches a distance field, Nectar Flow a volume; Detail is the budget for both.
    "curl_bloom", "nectar_flow", "crystal_canyon", "neon_city", "particle_tunnel", "chrome_nebula", "fractal_dive",
    "aurora_ocean", "spectrum_terrain", "menger_core", "gyroid_cavern", "jelly_abyss", "prism_rain",
};

constexpr std::array<Family, 9> kFamilies = {{
    // Fractal trees: branching folded distance fields, flown through or orbited.
    {"arbor", "arbor_", "lib_arbor",
     {"arbor_grove", "arbor_willow", "arbor_coral", "arbor_frost", "arbor_bolt", "arbor_roots", "arbor_bonsai", "arbor_canopy",
      "arbor_rosette", "arbor_spire", "arbor_synapse", "arbor_sakura"}},
    // Ray-marched power-N Mandelbulbs and their Julia relatives.
    {"bulb", "bulb_", "lib_bulb",
     {"bulb_classic", "bulb_julia", "bulb_slice", "bulb_lotus", "bulb_ink", "bulb_glow", "bulb_cavern", "bulb_shell",
      "bulb_prism", "bulb_contour", "bulb_chrome", "bulb_hybrid"}},
    // Polar petal fields and mandalas that bloom out of a glowing core.
    {"chrysanthemum", "chrys_", "lib_chrys",
     {"chrys_bloom", "chrys_phyllo", "chrys_dahlia", "chrys_lotus", "chrys_meadow", "chrys_corolla", "chrys_glass", "chrys_ink",
      "chrys_neon", "chrys_fall", "chrys_kaleido", "chrys_tunnel"}},
    // Nested domain warps, often folded into a kaleidoscope.
    {"lucid", "lucid_", "lib_lucid",
     {"lucid_weave", "lucid_mandala", "lucid_spiral", "lucid_tunnel", "lucid_disc", "lucid_hive", "lucid_ink", "lucid_mirror",
      "lucid_bulge", "lucid_petal", "lucid_lattice", "lucid_vortex"}},
    // Fly-through dives along five camera paths.
    {"dive", "dive_", "lib_dive",
     {"dive_lattice", "dive_jewel", "dive_mushroom", "dive_gyro", "dive_mandala", "dive_serpent", "dive_helix",
      "dive_accordion", "dive_orbit", "dive_neon", "dive_cycle", "dive_show"}},
    // Ray-traced glass: refraction, dispersion, thin films and caustic light.
    {"glass", "glass_", "lib_glass",
     {"glass_orb", "glass_pool", "glass_prism", "glass_lens", "glass_drops", "glass_bubbles", "glass_marble", "glass_caustic",
      "glass_cubes", "glass_gem", "glass_ice", "glass_knot"}},
    // Galaxies and gas clouds.
    {"nebula", "nebula_", "lib_nebula",
     {"nebula_dust", "nebula_spiral", "nebula_ring", "nebula_veil", "nebula_pulsar", "nebula_cluster", "nebula_nova",
      "nebula_horizon", "nebula_pillars", "nebula_flight", "nebula_storm", "nebula_web"}},
    // Ray-marched heightfield landscapes.
    {"terrain", "terra_", "lib_terra",
     {"terra_ridge", "terra_dunes", "terra_canyon", "terra_glacier", "terra_isles", "terra_mesa", "terra_volcano",
      "terra_terraces", "terra_moon", "terra_night", "terra_topo", "terra_wire"}},
    // Truchet tilings whose paths rewire on the beat.
    {"truchet", "truchet_", "lib_truchet",
     {"truchet_arcs", "truchet_hex", "truchet_weave", "truchet_maze", "truchet_tri", "truchet_quad", "truchet_tubes",
      "truchet_neon", "truchet_flow", "truchet_knot", "truchet_tunnel", "truchet_relief"}},
}};

// The family styles that march.
constexpr std::array<const char*, 51> kFamilyMarched = {
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
};

// The styles whose look is a turning one (kaleidoscopes, mandalas, spirals, vortices, tunnels),
// each with its own rate and direction so no two turn alike: a vortex winds fast, a dome barely
// creeps, tunnels counter. Every other style holds still unless the user turns it.
struct Spin {
    const char* id;
    float rate;
};
constexpr std::array<Spin, 20> kSpinning = {{
    {"kaleido", 1.0f},        {"mandala_dome", 0.45f},  {"orb_lattice", 0.7f},   {"chroma_orb", -0.6f},
    {"kifs", 0.8f},           {"spiral", 1.2f},         {"spiral_eye", -0.9f},   {"bead_vortex", 1.4f},
    {"merkaba_grid", 0.4f},   {"tunnel", -0.5f},        {"rod_tunnel", 0.6f},    {"particle_tunnel", -0.8f},
    {"lucid_mandala", 0.6f},  {"lucid_mirror", -0.7f},  {"lucid_spiral", 1.1f},  {"lucid_vortex", -1.3f},
    {"lucid_tunnel", 0.5f},   {"lucid_disc", 0.35f},    {"chrys_kaleido", -0.8f}, {"chrys_tunnel", 0.6f},
}};

constexpr const char* kMilkdrop = "milkdrop";
constexpr const char* kCurlFlow = "curlflow";
constexpr const char* kWater = "water";

struct KeyScope {
    const char* key;
    Scope scope;
};
// Every control that is NOT universal, keyed by its ParamKeys string (the label, the lock key and
// the search term at once). The composite pass applies the rest to the finished frame, so they
// reach every style.
constexpr KeyScope kKeyScopes[] = {
    {"Speed", Scope::SceneClock},
    {"Audio drive", Scope::AudioDrive},
    {"Bass gain", Scope::BassBand},
    {"Mid gain", Scope::MidBand},
    {"Treble gain", Scope::TrebleBand},
    {"Detail", Scope::MarchDetail},
    {"Morph", Scope::ShaderLook},
    {"Colour map", Scope::ShaderLook},
    {"Palette 2", Scope::ShaderLook},
    {"Palette blend", Scope::ShaderLook},
    {"Duotone", Scope::ShaderLook},
    {"Endless zoom", Scope::EndlessZoom},
    {"Dive speed", Scope::EndlessZoom},
    {"Turbulence", Scope::Turbulence},
    {"Density", Scope::DyeDensity},
    {"Trails", Scope::TrailToggle},
    {"Trail length", Scope::TrailLength},
    {"Trail zoom (echo in/out)", Scope::TrailEcho},
    {"Trail warp (liquid echo)", Scope::TrailEcho},
    {"Particle shape", Scope::ParticleSprite},
    {"Particle size", Scope::ParticleSprite},
    {"Particle life (s)", Scope::ParticleSprite},
    {"Particle drag", Scope::ParticleSprite},
    {"MilkDrop palette tint", Scope::Milkdrop},
    {"Blend preset changes", Scope::Milkdrop},
    {"Solver iterations", Scope::FluidSim},
    {"Pressure", Scope::FluidSim},
    {"Fluid curl", Scope::FluidSim},
    {"Motion fade", Scope::FluidSim},
    {"Fluid fade", Scope::FluidSim},
    {"Chromatic aging", Scope::FluidSim},
    {"Palette cycle", Scope::FluidSim},
    {"Particle brightness", Scope::FluidSim},
    {"Shading (embossed ink)", Scope::FluidSim},
    {"Glow (fluid)", Scope::FluidSim},
    {"Fluid glow", Scope::FluidSim},
    {"Glow threshold", Scope::FluidSim},
    {"Sunrays", Scope::FluidSim},
    {"Sunrays weight", Scope::FluidSim},
    {"Curl from mids", Scope::FluidSim},
    {"Glow from loudness", Scope::FluidSim},
    {"Fade when quiet", Scope::FluidSim},
    {"Hit pattern", Scope::Emitters},
    {"Hit splats", Scope::Emitters},
    {"Stirrers", Scope::Emitters},
    {"Stirrer speed", Scope::Emitters},
    {"Fluid splat radius", Scope::Emitters},
    {"Fluid splat force", Scope::Emitters},
    {"Bass pump", Scope::Emitters},
    {"Treble sparkle", Scope::Emitters},
    {"Radius on hit", Scope::Emitters},
    {"Path", Scope::Journey},
    {"Spawn points", Scope::Journey},
    {"Catch points", Scope::Journey},
    {"Catch pull", Scope::Journey},
    {"Catch radius", Scope::Journey},
    {"Flow force", Scope::FlowFieldSim},
    {"Flow curl", Scope::FlowFieldSim},
    {"Ripple strength", Scope::Water},
    {"Depth", Scope::Water},
    {"Specular", Scope::Water},
    {"Flow drift", Scope::Water},
    {"Liquid", Scope::Water},
    {"Liquid flow", Scope::Water},
    {"Liquid fade", Scope::Water},
    {"Ripple overlay strength", Scope::RippleOverlay},
    {"Ripple glint", Scope::RippleOverlay},
    {"Geometry", Scope::Cymatics},
    {"Fundamental (Hz)", Scope::Cymatics},
    {"Standing waves", Scope::Cymatics},
    {"Tonal focus", Scope::Cymatics},
    {"Plate ring", Scope::Cymatics},
    {"Field scale", Scope::Cymatics},
    {"Wave flow", Scope::Cymatics},
    {"Field swirl", Scope::Cymatics},
    {"Fill", Scope::Cymatics},
    {"Nodal lines", Scope::Cymatics},
    {"Nodal glow", Scope::Cymatics},
    {"Iridescence", Scope::Cymatics},
    {"Caustic sheen", Scope::Cymatics},
};

constexpr const char* kKindNames[kKindCount] = {"SHADER", "SILK",     "LIFE",  "MYCELIUM", "ACID",
                                                "MILKDROP", "FLUID", "CURL_FLOW", "WATER", "CYMATICS"};
constexpr const char* kScopeNames[kScopeCount] = {
    "UNIVERSAL",    "SCENE_CLOCK",     "AUDIO_DRIVE", "BASS_BAND", "MID_BAND",     "TREBLE_BAND", "BAND_GAINS", "SHADER_LOOK",
    "MARCH_DETAIL", "ENDLESS_ZOOM",    "TURBULENCE",  "DYE_DENSITY", "TRAIL_TOGGLE", "TRAIL_LENGTH", "TRAIL_ECHO",
    "PARTICLE_SPRITE", "MILKDROP",     "FLUID_SIM",   "EMITTERS",  "JOURNEY",      "WATER",       "CYMATICS",
    "FLOW_FIELD_SIM", "RIPPLE_OVERLAY",
};

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

// Per-band gain reaches only a style that reads that band's envelope; MilkDrop takes raw PCM.
bool readsBass(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Mycelium || k == Kind::Acid || k == Kind::Fluid || k == Kind::Water;
}
bool readsMid(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Acid || k == Kind::Fluid || k == Kind::CurlFlow || k == Kind::Water;
}
bool readsTreble(Kind k) { return k != Kind::Milkdrop; }

// Whether a scope reaches every style of a kind. MarchDetail is per style, so no kind has it.
bool reachesKind(Scope s, Kind k) {
    switch (s) {
        case Scope::Universal: return true;
        case Scope::SceneClock:
        case Scope::AudioDrive: return k != Kind::Milkdrop;
        case Scope::BassBand: return readsBass(k);
        case Scope::MidBand: return readsMid(k);
        case Scope::TrebleBand: return readsTreble(k);
        case Scope::BandGains: return readsBass(k) || readsMid(k) || readsTreble(k);
        case Scope::ShaderLook: return k == Kind::Shader;
        case Scope::MarchDetail: return false;
        case Scope::EndlessZoom: return k == Kind::Shader || k == Kind::Milkdrop;
        case Scope::Turbulence: return k == Kind::Shader || k == Kind::Mycelium || k == Kind::Acid || k == Kind::CurlFlow;
        case Scope::DyeDensity: return k == Kind::Fluid;
        case Scope::TrailToggle:
        case Scope::TrailLength: return k == Kind::Silk || k == Kind::CurlFlow;
        case Scope::TrailEcho: return k == Kind::CurlFlow;
        case Scope::ParticleSprite: return k == Kind::Fluid || k == Kind::CurlFlow;
        case Scope::Milkdrop: return k == Kind::Milkdrop;
        case Scope::FluidSim: return k == Kind::Fluid;
        case Scope::Emitters: return k == Kind::Fluid || k == Kind::Water;
        case Scope::Journey: return k == Kind::Fluid || k == Kind::CurlFlow || k == Kind::Water;
        case Scope::Water: return k == Kind::Water;
        case Scope::Cymatics: return k == Kind::Cymatics;
        case Scope::FlowFieldSim: return k != Kind::Fluid;
        case Scope::RippleOverlay: return k != Kind::Water;
    }
    return false;
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
        out += "style|" + id + "|" + name(kindOf(id));
        if (isMarched(id)) out += "|MARCH_DETAIL";
        out += '\n';
    }
    for (const auto& family : kFamilies) {
        out += std::string("family|") + family.name + "|" + family.prefix + "|" + family.library + "|";
        for (size_t i = 0; i < family.ids.size(); ++i) {
            if (i > 0) out += ',';
            out += family.ids[i];
        }
        out += '\n';
    }
    // A cymatics style that draws on one plate geometry overrides the user's Geometry control.
    for (const auto& id : styles::cymaticsIds()) {
        const int geometry = styles::cymatics(id)->geometryOverride;
        if (geometry >= 0) out += "override|" + id + "|Geometry|" + std::to_string(geometry) + "\n";
    }
    for (const auto& entry : kKeyScopes) out += std::string("key|") + entry.key + "|" + name(entry.scope) + "\n";
    const auto range = [&out](const char* key, float lo, float hi) {
        out += std::string("range|") + key + "|" + number(lo) + "|" + number(hi) + "\n";
    };
    range("Detail", kMarchDetail.min, kMarchDetail.max);
    range("Fundamental (Hz)", cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz);
    range("Standing waves", 1.0f, static_cast<float>(cymatics::kMaxRenderedModes));
    range("Zoom", kTouchZoom.min, kTouchZoom.max);
    range("Rotation", kTouchRotation.min, kTouchRotation.max);
    // The Fluid tab's quality chips are labelled in Kotlin; this says how many tiers they index.
    out += "value|fluidTiers|" + std::to_string(fluid::quality::kTierCount) + "\n";
    out += "value|touchRotationPerDegree|" + number(kTouchRotationPerDegree) + "\n";
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
        out.emplace_back(kWater);
        append(out, styles::cymaticsIds());
        return out;
    }();
    return kIds;
}

bool isShaderStyle(const std::string& id) { return listed(kLooseShaders, id) || familyStyle(id); }

bool knows(const std::string& id) {
    return isShaderStyle(id) || styles::cymatics(id) || styles::silk(id) || styles::life(id) || styles::acid(id) ||
           styles::myco(id) || styles::fluid(id) || id == kMilkdrop || id == kCurlFlow || id == kWater;
}

Kind kindOf(const std::string& id) {
    if (isShaderStyle(id)) return Kind::Shader;
    if (id == kMilkdrop) return Kind::Milkdrop;
    if (styles::fluid(id)) return Kind::Fluid;
    if (id == kCurlFlow) return Kind::CurlFlow;
    if (id == kWater) return Kind::Water;
    if (styles::cymatics(id)) return Kind::Cymatics;
    if (styles::life(id)) return Kind::Life;
    if (styles::myco(id)) return Kind::Mycelium;
    if (styles::acid(id)) return Kind::Acid;
    return Kind::Silk;
}

bool isMarched(const std::string& id) { return listed(kLooseMarched, id) || listed(kFamilyMarched, id); }

float spinRate(const std::string& id) {
    for (const Spin& known : kSpinning) {
        if (id == known.id) return known.rate;
    }
    return 0.0f;
}

const std::array<Family, 9>& families() { return kFamilies; }

Scope scopeOf(const std::string& paramKey) {
    for (const auto& entry : kKeyScopes) {
        if (paramKey == entry.key) return entry.scope;
    }
    return Scope::Universal;
}

bool reaches(Scope scope, const std::string& styleId) {
    if (scope == Scope::MarchDetail) return isMarched(styleId);
    return reachesKind(scope, kindOf(styleId));
}

const char* name(Kind kind) { return kKindNames[static_cast<int>(kind)]; }
const char* name(Scope scope) { return kScopeNames[static_cast<int>(scope)]; }

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::catalogue
