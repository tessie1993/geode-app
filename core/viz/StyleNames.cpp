#include "viz/StyleNames.hpp"

namespace geode::viz::catalogue {

namespace {

struct Name {
    const char* id;
    const char* label;
};

// The styles whose name is not their id in words.
constexpr Name kStyles[] = {
    {"cymatics", "Original · Resonant Field"},
    {"bessel_drum", "Drumhead"},
    {"rosensweig", "Rosensweig Spikes"},
    {"silk_web", "Halvorsen Web"},
    {"silk_bloom", "Cosine Bloom"},
    {"silk_weave", "Triaxial Weave"},
    {"silk_shell", "Concentric Shells"},
    {"silk_spiral", "Phase Spiral"},
    {"silk_fold", "Recursive Fold"},
    {"silk_hyper", "Hyperbolic Bloom"},
    {"silk_resonance", "Nested Resonance"},
    {"silk_curl", "Curl Weave"},
    {"silk_pendulum", "Pendulum Garden"},
    {"life_orbium", "Orbium Drift"},
    {"life_gyre", "Gyre Garden"},
    {"life_helix", "Helicium Reef"},
    {"life_pulsar", "Pulsar Colony"},
    {"life_hydro", "Hydrogeminium"},
    {"life_bug", "Smooth Bugs"},
    {"life_mitosis", "Mitosis"},
    {"life_coral", "Coral Bloom"},
    {"life_labyrinth", "Living Ink"},
    {"life_worms", "Ember Worms"},
    {"acid_tv", "TV Morphing"},
    {"acid_well", "Phosphor Well"},
    {"acid_kaleid", "Kaleido Melt"},
    {"acid_droste", "Droste Throat"},
    {"acid_prism", "Prism Drift"},
    {"acid_mosh", "Datamosh"},
    {"acid_scan", "Scanline Surge"},
    {"acid_solar", "Solar Flare"},
    {"acid_mirror", "Mirror Room"},
    {"acid_smear", "Neon Smear"},
    {"myco_polycephalum", "Polycephalum"},
    {"myco_rivals", "Rival Colonies"},
    {"myco_symbiosis", "Symbiosis"},
    {"myco_predator", "Predator"},
    {"myco_ghosts", "Ghost Veil"},
    {"myco_circuit", "Circuit Bloom"},
    {"myco_silkroad", "Silk Roads"},
    {"myco_sporestorm", "Spore Storm"},
    {"myco_capillary", "Capillaries"},
    {"myco_frostvein", "Frost Veins"},
    {"fluid", "Original · Dye"},
    {"fluid_ink", "Ink Wash"},
    {"fluid_oilslick", "Oil Slick"},
    {"fluid_neon", "Neon Contours"},
    {"fluid_chrome", "Liquid Chrome"},
    {"fluid_smoke", "Smoke"},
    {"fluid_lava", "Lava"},
    {"fluid_marble", "Marble"},
    {"fluid_aurora", "Aurora"},
};

// The families whose name is not their catalogue name with a capital.
constexpr Name kFamilies[] = {
    {"foam", "Sphere Foam"},
    {"milkdrop", "MilkDrop"},
};

char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

}  // namespace

std::string styleLabel(std::string_view id) {
    for (const auto& s : kStyles) {
        if (id == s.id) return s.label;
    }
    std::string out;
    bool start = true;
    for (char c : id) {
        if (c == '_') {
            out += ' ';
            start = true;
            continue;
        }
        out += start ? upper(c) : c;
        start = false;
    }
    return out;
}

std::string familyLabel(std::string_view name) {
    for (const auto& f : kFamilies) {
        if (name == f.id) return f.label;
    }
    std::string out(name);
    if (!out.empty()) out[0] = upper(out[0]);
    return out;
}

}  // namespace geode::viz::catalogue
