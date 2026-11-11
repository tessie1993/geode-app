#include "viz/CustomizeMap.hpp"


#include "viz/ParamRandomizer.hpp"

namespace geode::viz::customize {

namespace {

using C = Category;
using S = Show;
using catalogue::Kind;
using catalogue::Scope;

// The owner's design (issue #333): Basics are the two to four controls a listener reaches for
// first, and free; the sections under them go deeper, then Expert last. Order inside a section is
// the order the panel draws it.
const std::vector<Section>& table() {
    static const std::vector<Section> kSections = {
        {C::QuickKnobs, "basics", true, S::Always, {"@macros"}},
        {C::QuickKnobs, "sets", false, S::Always, {"@macro_sets"}},

        {C::Look, "basics", true, S::Always, {"Palette", "Brightness", "Saturation", "Bloom"}},
        {C::Look,
         "colour_detail",
         false,
         S::Always,
         {"Palette 2", "Palette blend", "Colour map", "Hue shift", "Hue range", "Temperature"}},
        {C::Look, "colour_cycle", false, S::Always, {"Color cycle", "Cycle speed"}},
        {C::Look, "light_detail", false, S::Always, {"Contrast", "Gamma", "Intensity"}},
        {C::Look, "palette_maker", false, S::Always, {"@palette_maker"}},

        {C::Shape, "basics", true, S::Always, {"Morph", "Kaleidoscope", "Detail"}},
        {C::Shape, "form", false, S::Always, {"Domain warp", "Twist", "Ripple", "Turbulence"}},
        {C::Shape, "symmetry", false, S::Always, {"Mirror", "Tile", "Pixelate"}},
        {C::Shape,
         "particles",
         false,
         S::Particles,
         {"Particle shape", "Particle size", "Density", "Particle life (s)", "Particle drag"}},
        // The style's own scene controls from the old Style page: where the particles and colour of
        // a Fluid, Curl Flow or Water style are born, and what the hits throw into it.
        {C::Shape,
         "journey",
         false,
         S::Always,
         {"Path", "Spawn points", "Progression", "Catch points", "Catch pull", "Catch radius"}},
        {C::Shape,
         "emitters",
         false,
         S::Always,
         {"Hit pattern", "Hit splats", "Stirrers", "Stirrer speed", "Fluid splat radius", "Radius on hit", "Fluid splat force",
          "Bass pump", "Treble sparkle", "Fluid quality", "Lower the quality when the phone struggles"}},

        {C::Motion, "basics", true, S::Always, {"Speed", "Zoom", "Rotation"}},
        {C::Motion, "drift", false, S::Always, {"Sway", "Drift X", "Drift Y"}},
        {C::Motion, "endless_dive", false, S::Always, {"Endless zoom", "Dive speed", "Travel zoom"}},
        {C::Motion, "inner_motion", false, S::Always, {"Motion amount", "Breath", "Spin drift"}},

        {C::Camera, "basics", true, S::Always, {"Camera path", "Fly speed"}},
        {C::Camera, "framing", false, S::ThreeD, {"Field of view", "Swing", "Push in", "Look turn", "Look tilt", "Roll"}},
        {C::Camera, "feel", false, S::ThreeD, {"Handheld shake", "Tilt phone to look", "Drag to look", "Camera smoothing"}},

        {C::Music, "basics", true, S::Always, {"Audio drive", "Reaction strength", "Reaction DNA", "@auto_moments"}},
        {C::Music, "moments", false, S::Always, {"@moments"}},
        {C::Music, "listening", false, S::Always, {"Bass gain", "Mid gain", "Treble gain", "@rise_fall"}},
        {C::Music, "notes_key", false, S::Always, {"Key colour", "@notes_key"}},
        // The modulation editors sit with the music they follow: MilkDrop keeps a preset's motion and
        // its per-frame reactions in one menu, and so does this page.
        {C::Music, "reactions", false, S::Always, {"@reactions"}},
        {C::Music, "envelopes", false, S::Always, {"@envelopes"}},
        {C::Music, "waves", false, S::Always, {"@waves"}},

        {C::Effects, "basics", true, S::Always, {"Trails", "Vignette", "@transition_style"}},
        {C::Effects, "trail_detail", false, S::Always, {"Trail length", "Trail zoom (echo in/out)", "Trail warp (liquid echo)"}},
        {C::Effects, "lens", false, S::Always, {"Chromatic aberration", "Fisheye"}},
        {C::Effects, "film", false, S::Always, {"Film grain", "Scanlines", "Posterize", "Duotone", "Solarize", "Invert"}},
        {C::Effects, "transition_detail", false, S::Always, {"@transition_length", "Glide time"}},

        {C::Liquid, "basics", true, S::Always, {"Flow field on", "Flow strength"}},
        {C::Liquid, "push_swirl", false, S::Always, {"Flow force", "Flow curl"}},
        {C::Liquid, "surface", false, S::Always, {"Born inside", "Colour age", "Surface"}},
        // Wave speed and Damping shape the ripple overlay on any style and the Water style's own
        // surface alike, so they sit here, where every style finds them.
        {C::Liquid,
         "ripples",
         false,
         S::Always,
         {"Water ripples on", "Ripple overlay strength", "Ripple glint", "Wave speed", "Damping"}},
        {C::Liquid,
         "water",
         false,
         S::Water,
         {"Ripple strength", "Depth", "Specular", "Flow drift", "Liquid", "Liquid flow", "Liquid fade"}},

        // One section per engine, split so no list runs past a dozen rows; each shows only where its
        // controls reach.
        {C::Expert,
         "fluid_flow",
         false,
         S::Engine,
         {"Solver iterations", "Fluid curl", "Motion fade", "Fluid fade", "Chromatic aging", "Pressure",
          "Curl from mids", "Fade when quiet"}},
        {C::Expert,
         "fluid_look",
         false,
         S::Engine,
         {"Shading (embossed ink)", "Glow (fluid)", "Fluid glow", "Glow threshold", "Glow from loudness",
          "Sunrays", "Sunrays weight", "Palette cycle", "Particle brightness", "Particle layer", "Ink layer"}},
        {C::Expert,
         "plate",
         false,
         S::Engine,
         {"Geometry", "Fundamental (Hz)", "Standing waves", "Tonal focus", "Plate ring", "Field scale", "Wave flow",
          "Field swirl"}},
        {C::Expert,
         "plate_look",
         false,
         S::Engine,
         {"Fill", "Nodal lines", "Nodal glow", "Iridescence", "Caustic sheen"}},
        {C::Expert,
         "milkdrop",
         false,
         S::Engine,
         {"Blend preset changes", "MilkDrop palette tint"}},
        {C::Expert, "layers", false, S::Always, {"@layers"}},
        {C::Expert, "shader_code", false, S::ShaderCode, {"@shader_code"}},
    };
    return kSections;
}

const std::vector<HeldControl>& held() {
    static const std::vector<HeldControl> kHeld = {
        {"Detail", {"marchDetail"}, std::nullopt},
        {"Travel zoom", {"camFlat"}, std::nullopt},
        {"Push in", {"camPush"}, std::nullopt},
        {"Look turn", {"camYaw"}, std::nullopt},
        {"Look tilt", {"camPitch"}, std::nullopt},
        {"Roll", {"camRoll"}, std::nullopt},
        {"Camera smoothing", {"camSmooth"}, std::nullopt},
        {"Drag to look", {"camTouch"}, std::nullopt},
        {"Tilt phone to look", {"camGyro"}, std::nullopt},
        {"Reaction strength", {"reactAmount"}, std::nullopt},
        {"Flow field on", {"flowEnabled"}, std::nullopt},
        {"Born inside", {"liquidBorn"}, std::nullopt},
        {"Colour age", {"liquidAge"}, std::nullopt},
        {"Surface", {"liquidSurface"}, std::nullopt},
        {"Water ripples on", {"rippleOverlayEnabled"}, Scope::RippleOverlay},
        {"Glide time", {"paramFadeSec"}, std::nullopt},
        {"Progression", {"fluidSpawnProgress"}, Scope::Journey},
        {"Fluid quality", {"fluidQuality"}, Scope::Emitters},
        {"Lower the quality when the phone struggles", {"fluidAutoQuality"}, Scope::Emitters},
        {"Particle layer", {"fluidParticlesEnabled"}, Scope::FluidSim},
        {"Ink layer", {"fluidDyeEnabled"}, Scope::FluidSim},
        // Panel pieces that are not one parameter. Notes & key's amounts are SceneParams values the
        // page draws as a block, so its Reset puts them back.
        {"@macros", {}, std::nullopt},
        {"@macro_sets", {}, std::nullopt},
        {"@palette_maker", {}, std::nullopt},
        {"@auto_moments", {}, std::nullopt},
        {"@moments", {}, std::nullopt},
        {"@rise_fall", {}, Scope::BandGains},
        {"@notes_key", {"noteColor", "notePitch", "noteIntervals", "noteTension", "noteChords", "noteDrums"}, std::nullopt},
        {"@transition_style", {}, std::nullopt},
        {"@transition_length", {}, std::nullopt},
        {"@reactions", {}, std::nullopt},
        {"@envelopes", {}, std::nullopt},
        {"@waves", {}, std::nullopt},
        {"@layers", {}, std::nullopt},
        {"@shader_code", {}, std::nullopt},
    };
    return kHeld;
}

constexpr const char* kCategoryNames[] = {"QUICK_KNOBS", "LOOK",    "SHAPE",  "MOTION", "CAMERA",
                                          "MUSIC",       "EFFECTS", "LIQUID", "EXPERT"};
constexpr const char* kShowNames[] = {"ALWAYS", "THREE_D", "PARTICLES", "WATER", "ENGINE", "SHADER_CODE"};

const randomizer::RolledKey* rolled(std::string_view key) {
    for (const auto& roll : randomizer::rolledKeys()) {
        if (key == roll.key) return &roll;
    }
    return nullptr;
}

const HeldControl* heldNamed(std::string_view key) {
    for (const auto& h : held()) {
        if (key == h.key) return &h;
    }
    return nullptr;
}

bool rule(Show show, const std::string& styleId) {
    if (styleId.empty()) return true;
    const Kind kind = catalogue::kindOf(styleId);
    switch (show) {
        case Show::Always: return true;
        case Show::ThreeD: return catalogue::fliesCamera(styleId);
        case Show::Particles: return catalogue::reaches(Scope::ParticleSprite, styleId);
        case Show::Water: return kind == Kind::Water;
        case Show::Engine: return kind == Kind::Fluid || kind == Kind::Cymatics || kind == Kind::Milkdrop;
        case Show::ShaderCode: return kind == Kind::Shader || kind == Kind::Fluid;
    }
    return true;
}

// Camera shows on every style now: Fly speed and the path pan and tilt a flat picture too. Only its
// framing and feel sections stay with the styles on the camera rig.
Show categoryRule(Category) { return Show::Always; }

}  // namespace

const std::vector<Section>& sections() { return table(); }

const std::vector<HeldControl>& heldControls() { return held(); }

std::optional<Category> categoryNamed(std::string_view categoryName) {
    for (int i = 0; i < kCategoryCount; ++i) {
        if (categoryName == kCategoryNames[i]) return static_cast<Category>(i);
    }
    return std::nullopt;
}

const char* name(Category category) { return kCategoryNames[static_cast<int>(category)]; }

const char* name(Show show) { return kShowNames[static_cast<int>(show)]; }

std::optional<Category> categoryOf(std::string_view key) {
    for (const auto& section : table()) {
        for (const char* control : section.controls) {
            if (key == control) return section.category;
        }
    }
    return std::nullopt;
}

std::set<std::string> expandLocks(const std::set<std::string>& locked) {
    std::set<std::string> out;
    for (const auto& key : locked) {
        if (key.empty() || key[0] != kCategoryLockPrefix) {
            out.insert(key);
            continue;
        }
        const auto category = categoryNamed(std::string_view(key).substr(1));
        if (!category) continue;
        for (const auto& section : table()) {
            if (section.category != *category) continue;
            for (const char* control : section.controls) out.insert(control);
        }
    }
    return out;
}

bool reaches(std::string_view key, const std::string& styleId) {
    if (styleId.empty()) return true;
    if (const auto* h = heldNamed(key); h && h->scope) return catalogue::reaches(*h->scope, styleId);
    return catalogue::reaches(catalogue::scopeOf(std::string(key)), styleId);
}

bool shows(const Section& section, const std::string& styleId) {
    if (!rule(section.show, styleId) || !rule(categoryRule(section.category), styleId)) return false;
    for (const char* control : section.controls) {
        if (reaches(control, styleId)) return true;
    }
    return false;
}

bool shows(Category category, const std::string& styleId) {
    for (const auto& section : table()) {
        if (section.category == category && shows(section, styleId)) return true;
    }
    return false;
}

std::vector<const char*> fieldsOf(std::string_view key) {
    std::vector<const char*> out;
    if (const auto* roll = rolled(key)) {
        for (const char* field : roll->fields) {
            if (field) out.push_back(field);
        }
    } else if (const auto* h = heldNamed(key)) {
        out = h->fields;
    }
    return out;
}

SceneParams randomize(const SceneParams& current, uint64_t seed, Category category, const std::string& styleId,
                      const std::set<std::string>& locked) {
    // The roll table's own groups do not line up with the categories, so roll every group and hold
    // what this category does not own, the same way a lock holds a control.
    std::set<std::string> held = locked;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (categoryOf(roll.key) != category) held.insert(roll.key);
    }
    return randomizer::randomize(current, seed, std::nullopt, styleId, held);
}

SceneParams borrow(const SceneParams& current, const SceneParams& from, Category category) {
    SceneParams out = current;
    for (const auto& section : table()) {
        if (section.category != category) continue;
        for (const char* control : section.controls) {
            for (const char* field : fieldsOf(control)) out.set(field, from.get(field));
        }
    }
    return out;
}

SceneParams reset(const SceneParams& current, Category category) { return borrow(current, SceneParams{}, category); }

const std::string& text() {
    static const std::string kText = [] {
        std::string out = "customize|" + std::to_string(kTextVersion) + "\n";
        for (int i = 0; i < kCategoryCount; ++i) {
            const auto category = static_cast<Category>(i);
            out += std::string("category|") + name(category) + "|" + name(categoryRule(category)) + "\n";
        }
        for (const auto& section : table()) {
            out += std::string("section|") + name(section.category) + "|" + section.id + "|" + (section.basics ? "BASICS" : "PRO") +
                   "|" + name(section.show) + "|";
            for (size_t i = 0; i < section.controls.size(); ++i) {
                if (i > 0) out += ',';
                out += section.controls[i];
            }
            out += '\n';
        }
        return out;
    }();
    return kText;
}

std::string shownOn(const std::string& styleId) {
    std::string out;
    for (int i = 0; i < kCategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        if (!shows(category, styleId)) continue;
        out += std::string(name(category)) + "\n";
        for (const auto& section : table()) {
            if (section.category == category && shows(section, styleId)) {
                out += std::string(name(category)) + "/" + section.id + "\n";
            }
        }
    }
    return out;
}

}  // namespace geode::viz::customize
