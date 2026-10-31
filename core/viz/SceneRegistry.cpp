#include "viz/SceneRegistry.hpp"

#include <array>

#include "viz/scenes/AcidScene.hpp"
#include "viz/scenes/CurlFlowScene.hpp"
#include "viz/scenes/CymaticsScene.hpp"
#include "viz/scenes/FluidScene.hpp"
#include "viz/scenes/LifeScene.hpp"
#include "viz/scenes/MilkdropScene.hpp"
#include "viz/scenes/MycoScene.hpp"
#include "viz/scenes/ShaderScene.hpp"
#include "viz/scenes/SilkScene.hpp"
#include "viz/scenes/StyleCatalog.hpp"
#include "viz/scenes/WaterScene.hpp"

namespace geode::viz {

namespace {

// Port of SceneCapabilities.SHADER_SCENES: id -> shaders/<id>_frag.glsl, in the catalog's order.
constexpr std::array<const char*, 149> kShaderIds = {
    "julia",     "tunnel",  "mandel",    "kaleido",   "plasma",    "bars",       "ring",      "scope",
    "liss",      "warp",    "grid",      "voronoi",   "metaballs", "ripples",    "starfield", "waves",
    "hexgrid",   "spiral",  "aurora",    "solar",     "winter",    "lava",       "vanishing", "morphogen",
    "nebula",    "noneuclid", "kifs",    "orb_lattice", "rod_tunnel", "neon_tiles",
    "chroma_orb", "mandala_dome", "bead_vortex",
    "merkaba_grid", "spiral_eye", "blacklight_bloom", "fractal_temple",
    "curl_bloom", "nectar_flow",
    "crystal_canyon", "neon_city", "particle_tunnel", "chrome_nebula", "fractal_dive", "aurora_ocean",
    "spectrum_terrain", "outrun_drive", "menger_core", "gyroid_cavern",
    "ringed_planet", "jelly_abyss", "ribbon_storm", "prism_rain",
    // style families (StyleFamilies.kt), in family order
    "arbor_grove", "arbor_willow", "arbor_coral", "arbor_frost", "arbor_bolt", "arbor_roots",
    "arbor_bonsai", "arbor_canopy", "arbor_rosette", "arbor_spire", "arbor_synapse", "arbor_sakura",
    "bulb_classic", "bulb_julia", "bulb_slice", "bulb_lotus", "bulb_ink", "bulb_glow",
    "bulb_cavern", "bulb_shell", "bulb_prism", "bulb_contour", "bulb_chrome", "bulb_hybrid",
    "chrys_bloom", "chrys_phyllo", "chrys_dahlia", "chrys_lotus", "chrys_meadow", "chrys_corolla",
    "chrys_glass", "chrys_ink", "chrys_neon", "chrys_fall", "chrys_kaleido", "chrys_tunnel",
    "lucid_weave", "lucid_mandala", "lucid_spiral", "lucid_tunnel", "lucid_disc", "lucid_hive",
    "lucid_ink", "lucid_mirror", "lucid_bulge", "lucid_petal", "lucid_lattice", "lucid_vortex",
    "dive_lattice", "dive_jewel", "dive_mushroom", "dive_gyro", "dive_mandala", "dive_serpent",
    "dive_helix", "dive_accordion", "dive_orbit", "dive_neon", "dive_cycle", "dive_show",
    "glass_orb", "glass_pool", "glass_prism", "glass_lens", "glass_drops", "glass_bubbles",
    "glass_marble", "glass_caustic", "glass_cubes", "glass_gem", "glass_ice", "glass_knot",
    "nebula_dust", "nebula_spiral", "nebula_ring", "nebula_veil", "nebula_pulsar", "nebula_cluster",
    "nebula_nova", "nebula_horizon", "nebula_pillars", "nebula_flight", "nebula_storm", "nebula_web",
    "terra_ridge", "terra_dunes", "terra_canyon", "terra_glacier", "terra_isles", "terra_mesa",
    "terra_volcano", "terra_terraces", "terra_moon", "terra_night", "terra_topo", "terra_wire",
};

// Port of SceneCapabilities.SPINNING_SCENES: the styles whose look is a turning
// one, each with its own rate and direction for the motion field's drift spin so
// no two turn alike (a vortex winds fast, a dome barely creeps, tunnels counter).
struct Spin {
    const char* id;
    float rate;
};
constexpr std::array<Spin, 20> kSpinningIds = {{
    {"kaleido", 1.0f},
    {"mandala_dome", 0.45f},
    {"orb_lattice", 0.7f},
    {"chroma_orb", -0.6f},
    {"kifs", 0.8f},
    {"spiral", 1.2f},
    {"spiral_eye", -0.9f},
    {"bead_vortex", 1.4f},
    {"merkaba_grid", 0.4f},
    {"tunnel", -0.5f},
    {"rod_tunnel", 0.6f},
    {"particle_tunnel", -0.8f},
    {"lucid_mandala", 0.6f},
    {"lucid_mirror", -0.7f},
    {"lucid_spiral", 1.1f},
    {"lucid_vortex", -1.3f},
    {"lucid_tunnel", 0.5f},
    {"lucid_disc", 0.35f},
    {"chrys_kaleido", -0.8f},
    {"chrys_tunnel", 0.6f},
}};
constexpr const char* kMilkdrop = "milkdrop";
constexpr const char* kCurlFlow = "curlflow";
constexpr const char* kWater = "water";

bool isShaderId(const std::string& id) {
    for (const char* known : kShaderIds) {
        if (id == known) return true;
    }
    return false;
}

void append(std::vector<std::string>& out, std::vector<std::string> ids) {
    out.insert(out.end(), ids.begin(), ids.end());
}

}  // namespace

bool SceneRegistry::isShaderStyle(const std::string& id) { return isShaderId(id); }

float SceneRegistry::spinRate(const std::string& id) {
    for (const Spin& known : kSpinningIds) {
        if (id == known.id) return known.rate;
    }
    return 0.0f;
}

bool SceneRegistry::knows(const std::string& id) const {
    return isShaderId(id) || styles::cymatics(id) || styles::silk(id) || styles::life(id) || styles::acid(id) || styles::myco(id) ||
           styles::fluid(id) || id == kMilkdrop || id == kCurlFlow || id == kWater;
}

std::vector<std::string> SceneRegistry::availableIds() const {
    std::vector<std::string> out;
    append(out, styles::silkIds());
    append(out, styles::lifeIds());
    append(out, styles::mycoIds());
    append(out, styles::acidIds());
    out.insert(out.end(), kShaderIds.begin(), kShaderIds.end());
    out.emplace_back(kMilkdrop);
    append(out, styles::fluidIds());
    out.emplace_back(kCurlFlow);
    out.emplace_back(kWater);
    append(out, styles::cymaticsIds());
    return out;
}

std::unique_ptr<Scene> SceneRegistry::create(const std::string& id, const std::string& quadVert) const {
    if (isShaderId(id)) {
        std::string error;
        const auto fragment = loader_.assets.load(id + "_frag.glsl", &error);
        if (!fragment) {
            host_.onShaderError(error);
            return nullptr;
        }
        return std::make_unique<ShaderScene>(id, quadVert, *fragment, loader_.cache, host_);
    }
    if (const auto* style = styles::cymatics(id)) return std::make_unique<CymaticsScene>(*style, loader_, host_);
    if (const auto* style = styles::silk(id)) return std::make_unique<SilkScene>(*style, loader_, profile_, host_);
    if (const auto* style = styles::life(id)) return std::make_unique<LifeScene>(*style, loader_, host_);
    if (const auto* style = styles::acid(id)) return std::make_unique<AcidScene>(*style, loader_, host_);
    if (const auto* style = styles::myco(id)) return std::make_unique<MycoScene>(*style, loader_, host_);
    if (id == kMilkdrop) return std::make_unique<MilkdropScene>(loader_, host_);
    if (const auto* style = styles::fluid(id)) return std::make_unique<FluidScene>(*style, loader_, host_);
    if (id == kCurlFlow) return std::make_unique<CurlFlowScene>(loader_, host_);
    if (id == kWater) return std::make_unique<WaterScene>(loader_, host_);
    return nullptr;
}

}  // namespace geode::viz
