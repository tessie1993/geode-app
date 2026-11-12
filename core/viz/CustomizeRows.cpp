#include "viz/CustomizeRows.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "viz/Catalogue.hpp"
#include "viz/ControlRange.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/PanelText.hpp"
#include "viz/SliderMap.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::customize {

namespace {

using W = Widget;
using R = Rule;

constexpr int kOwn = -1;
constexpr int kSeconds = 2;  // ParamUnit::Seconds

// Every control the map places, as the panel used to draw it (was the shell's CustomizeControls).
// A label is the words on the row where they differ from the key, the key the user keeps and searches by.
const std::vector<Control>& table() {
    static const std::vector<Control> kControls = {
        // Look
        {"Palette", W::Palette, "palette", nullptr, kOwn, R::Always, false},
        {"Brightness", W::Slider, "brightness", nullptr, kOwn, R::Always, false},
        {"Saturation", W::Slider, "saturation", nullptr, kOwn, R::Always, false},
        {"Bloom", W::Slider, "bloom", "Glow", kOwn, R::Always, false},
        {"Hue shift", W::Slider, "colorShift", nullptr, kOwn, R::Always, false},
        {"Hue range", W::Slider, "hueRange", nullptr, kOwn, R::Always, false},
        {"Temperature", W::Slider, "temperature", "Warmth", kOwn, R::Always, false},
        {"Contrast", W::Slider, "contrast", nullptr, kOwn, R::Always, false},
        {"Gamma", W::Slider, "gamma", "Midtones", kOwn, R::Always, false},
        {"Intensity", W::Slider, "intensity", nullptr, kOwn, R::Always, false},
        // The shading pass (lib_shade): the key's soft shadows (catalogue::Scope::Shadow) and the escape colour
        // (catalogue::Scope::Escape) on the raymarched families.
        {"Soft shadows", W::Slider, "shadow", nullptr, kOwn, R::Always, false},
        {"Escape colour", W::Slider, "escapeColor", nullptr, kOwn, R::Always, false},
        // Shape
        {"Morph", W::Slider, "morph", nullptr, kOwn, R::Always, false},
        // Whole folds: native words the value ("Off" at 1, else the count) and rounds it for the shaders.
        {"Kaleidoscope", W::Slider, "kaleidoFolds", nullptr, kOwn, R::Always, false},
        {"Detail", W::Slider, "marchDetail", nullptr, kOwn, R::Always, false},
        {"Domain warp", W::Slider, "warp", "Warp", kOwn, R::Always, false},
        {"Twist", W::Slider, "twist", nullptr, kOwn, R::Always, false},
        {"Ripple", W::Slider, "ripple", nullptr, kOwn, R::Always, false},
        {"Turbulence", W::Slider, "turbulence", nullptr, kOwn, R::Always, false},
        {"Mirror", W::Check, "mirror", nullptr, kOwn, R::Always, false},
        {"Tile", W::Slider, "tile", nullptr, kOwn, R::Always, false},
        {"Pixelate", W::Slider, "pixelate", nullptr, kOwn, R::Always, false},
        // Grow & layers: shapes the family libraries draw (catalogue::Scope::FamilyForm), Formula the styles that keep formulas.
        {"Spawn", W::Slider, "spawn", nullptr, kOwn, R::Always, false},
        {"Fractal", W::Slider, "fractal", nullptr, kOwn, R::Always, false},
        {"Front layer", W::Slider, "frontLayer", nullptr, kOwn, R::Always, false},
        {"Back layer", W::Slider, "backLayer", nullptr, kOwn, R::Always, false},
        {"Formula", W::Slider, "formula", nullptr, kOwn, R::Always, false},
        // Shapes: how far the shape graph melts into a raymarched style's form (catalogue::Scope::Shapes).
        {"Shapes", W::Slider, "shapes", nullptr, kOwn, R::Always, false},
        {"Particle shape", W::Chips, "particleShape", nullptr, kOwn, R::Always, false},
        {"Particle size", W::Slider, "particleSize", nullptr, kOwn, R::Always, false},
        {"Density", W::Slider, "density", nullptr, kOwn, R::Always, false},
        {"Particle life (s)", W::Slider, "fluidParticleLife", nullptr, kOwn, R::Always, false},
        {"Particle drag", W::Slider, "fluidParticleDrag", nullptr, kOwn, R::Always, false},
        {"Path", W::Chips, "fluidSpawnPath", nullptr, kOwn, R::Always, false},
        {"Spawn points", W::IntSlider, "fluidSpawnPoints", nullptr, kOwn, R::Always, false},
        {"Progression", W::Slider, "fluidSpawnProgress", nullptr, kOwn, R::Always, false},
        {"Catch points", W::IntSlider, "fluidCatchPoints", nullptr, kOwn, R::Always, false},
        {"Catch pull", W::Slider, "fluidCatchPull", nullptr, kOwn, R::CatchPoints, false},
        {"Catch radius", W::Slider, "fluidCatchRadius", nullptr, kOwn, R::CatchPoints, false},
        {"Hit pattern", W::Chips, "fluidBeatPattern", nullptr, kOwn, R::Always, false},
        {"Hit splats", W::IntSlider, "fluidBeatSplats", nullptr, kOwn, R::Always, false},
        {"Stirrers", W::IntSlider, "fluidStirrers", nullptr, kOwn, R::Always, false},
        {"Stirrer speed", W::Slider, "fluidStirrerSpeed", nullptr, kOwn, R::Always, false},
        {"Fluid splat radius", W::Slider, "fluidSplatRadius", nullptr, kOwn, R::Always, false},
        {"Radius on hit", W::Slider, "fluidRadiusPulse", nullptr, kOwn, R::Always, false},
        {"Fluid splat force", W::Slider, "fluidSplatForce", nullptr, kOwn, R::Always, false},
        {"Bass pump", W::Check, "fluidBassPump", nullptr, kOwn, R::Always, false},
        {"Treble sparkle", W::Check, "fluidSparkle", nullptr, kOwn, R::Always, false},
        {"Fluid quality", W::Pick, "fluidQuality", nullptr, kOwn, R::Always, false},
        {"Lower the quality when the phone struggles", W::Check, "fluidAutoQuality", nullptr, kOwn, R::Always, false},
        // Motion
        {"Speed", W::Slider, "speed", nullptr, kOwn, R::Always, false},
        {"Zoom", W::Slider, "zoom", nullptr, kOwn, R::Always, false},
        {"Rotation", W::Slider, "rotation", "Spin", kOwn, R::Always, false},
        {"Sway", W::Slider, "sway", nullptr, kOwn, R::Always, false},
        {"Drift X", W::Slider, "driftX", "Drift sideways", kOwn, R::Always, false},
        {"Drift Y", W::Slider, "driftY", "Drift up and down", kOwn, R::Always, false},
        {"Motion amount", W::Slider, "motionAmount", nullptr, kOwn, R::Always, false},
        {"Breath", W::Slider, "motionBreath", nullptr, kOwn, R::Always, false},
        {"Spin", W::Slider, "motionDrift", nullptr, kOwn, R::Always, false},
        // Dive
        {"Endless zoom", W::Check, "endlessZoom", nullptr, kOwn, R::Always, false},
        {"Dive speed", W::Slider, "endlessZoomSpeed", nullptr, kOwn, R::DiveOrLiquid, false},
        {"Ring spacing", W::Slider, "diveSpacing", nullptr, kOwn, R::Always, false},
        {"Tunnel shape", W::Slider, "diveShape", nullptr, kOwn, R::Always, false},
        {"Dive blend", W::Slider, "diveBlend", "Soft edges", kOwn, R::Always, false},
        {"Depth fade", W::Slider, "diveFog", nullptr, kOwn, R::Always, false},
        {"Dive twist", W::Slider, "diveTwist", "Spiral", kOwn, R::Always, false},
        {"Dive aim X", W::Slider, "diveAimX", "Aim sideways", kOwn, R::Always, false},
        {"Dive aim Y", W::Slider, "diveAimY", "Aim up and down", kOwn, R::Always, false},
        {"Dive lens", W::Slider, "diveLens", "Lens", kOwn, R::Always, false},
        {"Music follow", W::Slider, "diveFollow", nullptr, kOwn, R::Always, false},
        {"Travel zoom", W::Slider, "camFlat", nullptr, kOwn, R::Always, false},
        // Camera. Below zero Fly travels backward, and a flat style zooms out instead of in.
        {"Camera path", W::Chips, "camPath", nullptr, kOwn, R::Always, false},
        {"Fly speed", W::Slider, "camFly", nullptr, kOwn, R::Always, false},
        {"Field of view", W::Slider, "camFov", nullptr, kOwn, R::Always, false},
        {"Swing", W::Slider, "camOrbit", nullptr, kOwn, R::Always, false},
        {"Push in", W::Slider, "camPush", nullptr, kOwn, R::Always, false},
        {"Look turn", W::Slider, "camYaw", nullptr, kOwn, R::Always, false},
        {"Look tilt", W::Slider, "camPitch", nullptr, kOwn, R::Always, false},
        {"Roll", W::Slider, "camRoll", nullptr, kOwn, R::Always, false},
        {"Handheld shake", W::Slider, "camShake", nullptr, kOwn, R::Always, false},
        {"Tilt phone to look", W::Check, "camGyro", nullptr, kOwn, R::Always, false},
        // One finger never reaches the visual (ui/VisualGestures); the look follows a two-finger drag.
        {"Drag to look", W::Check, "camTouch", "Two-finger drag to look", kOwn, R::Always, false},
        {"Camera smoothing", W::Slider, "camSmooth", nullptr, kOwn, R::Always, false},
        // Music
        {"Audio drive", W::Slider, "audioDrive", "Sound strength", kOwn, R::Always, false},
        {"Reaction strength", W::Slider, "reactAmount", nullptr, kOwn, R::Always, false},
        {"Reaction DNA", W::Piece, "", "customize_personality", kOwn, R::Always, false},
        {"Bass gain", W::Slider, "bassGain", "Bass", kOwn, R::Always, false},
        {"Mid gain", W::Slider, "midGain", "Mids", kOwn, R::Always, false},
        {"Treble gain", W::Slider, "trebGain", "Highs", kOwn, R::Always, false},
        {"Key colour", W::Slider, "motionHue", nullptr, kOwn, R::Always, false},
        // Effects
        {"Trails", W::Check, "trails", nullptr, kOwn, R::Always, false},
        {"Vignette", W::Slider, "vignette", "Dark edges", kOwn, R::Always, false},
        {"Trail length", W::Slider, "trailLength", nullptr, kOwn, R::Always, false},
        {"Trail zoom (echo in/out)", W::Slider, "trailZoom", "Trail zoom", kOwn, R::Always, false},
        {"Trail warp (liquid echo)", W::Slider, "trailWarp", "Trail ripple", kOwn, R::Always, false},
        {"Chromatic aberration", W::Slider, "chromaAb", "Colour fringe", kOwn, R::Always, false},
        {"Fisheye", W::Slider, "fisheye", "Bulge", kOwn, R::Always, false},
        {"Film grain", W::Slider, "grain", nullptr, kOwn, R::Always, false},
        {"Scanlines", W::Slider, "scanlines", nullptr, kOwn, R::Always, false},
        {"Posterize", W::Slider, "posterize", "Fewer colours", kOwn, R::Always, false},
        {"Duotone", W::Check, "duotone", "Two-colour", kOwn, R::Always, false},
        {"Solarize", W::Check, "solarize", nullptr, kOwn, R::Always, false},
        {"Invert", W::Check, "invert", "Invert colours", kOwn, R::Always, false},
        {"Glide time", W::Slider, "paramFadeSec", nullptr, kSeconds, R::Always, false},
        // Liquid
        {"Flow field on", W::Liquid, "flowEnabled", "Liquid mode", kOwn, R::Always, false},
        {"Flow strength", W::Slider, "flowStrength", "Liquidity", kOwn, R::Liquid, false},
        {"Flow force", W::Slider, "flowForce", "Push", kOwn, R::Liquid, false},
        {"Flow curl", W::Slider, "flowCurl", "Swirl", kOwn, R::Liquid, false},
        {"Born inside", W::Chips, "liquidBorn", nullptr, kOwn, R::Always, false},
        {"Colour age", W::Chips, "liquidAge", nullptr, kOwn, R::Always, false},
        {"Surface", W::Chips, "liquidSurface", nullptr, kOwn, R::Always, false},
        // Engine: Fluid's solver and look, Cymatics' plate, MilkDrop's hand-over.
        {"Solver iterations", W::IntSlider, "fluidIterations", "Accuracy", kOwn, R::Always, false},
        {"Fluid curl", W::Slider, "fluidCurl", "Fluid swirl", kOwn, R::FluidStyle, false},
        {"Motion fade", W::Slider, "fluidVelocityDissipation", nullptr, kOwn, R::Always, false},
        {"Fluid fade", W::Slider, "fluidDensityDissipation", nullptr, kOwn, R::Always, false},
        {"Chromatic aging", W::Slider, "fluidChromaticAging", "Colour ageing", kOwn, R::Always, false},
        {"Pressure", W::Slider, "fluidPressure", nullptr, kOwn, R::Always, false},
        {"Curl from mids", W::Slider, "fluidCurlAudio", "Swirl from the mids", kOwn, R::Always, false},
        {"Fade when quiet", W::Slider, "fluidFadeAudio", nullptr, kOwn, R::Always, false},
        {"Shading (embossed ink)", W::Check, "fluidShading", "Embossed ink", kOwn, R::Always, false},
        {"Glow (fluid)", W::Check, "fluidBloom", "Fluid glow", kOwn, R::Always, false},
        {"Fluid glow", W::Slider, "fluidBloomIntensity", "Glow amount", kOwn, R::FluidGlow, false},
        {"Glow threshold", W::Slider, "fluidBloomThreshold", nullptr, kOwn, R::FluidGlow, false},
        {"Glow from loudness", W::Slider, "fluidBloomAudio", "Glow from volume", kOwn, R::Always, false},
        {"Sunrays", W::Check, "fluidSunrays", nullptr, kOwn, R::Always, false},
        {"Sunrays weight", W::Slider, "fluidSunraysWeight", "Sunray strength", kOwn, R::Sunrays, false},
        {"Palette cycle", W::Slider, "fluidPaletteCycleSpeed", nullptr, kOwn, R::Always, false},
        {"Particle brightness", W::Slider, "fluidParticleBrightness", nullptr, kOwn, R::Always, false},
        {"Particle layer", W::Check, "fluidParticlesEnabled", nullptr, kOwn, R::Always, false},
        {"Ink layer", W::Check, "fluidDyeEnabled", nullptr, kOwn, R::Always, false},
        {"Geometry", W::Geometry, "cymaticsGeometry", nullptr, kOwn, R::Always, false},
        {"Fundamental (Hz)", W::Slider, "cymaticsFundamental", "Base note (Hz)", kOwn, R::Always, true},
        {"Standing waves", W::IntSlider, "cymaticsModes", nullptr, kOwn, R::Always, false},
        {"Tonal focus", W::Slider, "cymaticsFocus", nullptr, kOwn, R::Always, false},
        {"Plate ring", W::Slider, "cymaticsRing", nullptr, kOwn, R::Always, false},
        {"Field scale", W::Slider, "cymaticsScale", nullptr, kOwn, R::Always, false},
        {"Wave flow", W::Slider, "cymaticsFlow", nullptr, kOwn, R::Always, false},
        {"Field swirl", W::Slider, "cymaticsSwirl", nullptr, kOwn, R::Always, false},
        {"Fill", W::Slider, "cymaticsFill", nullptr, kOwn, R::Always, false},
        {"Nodal lines", W::Slider, "cymaticsLine", "Still lines", kOwn, R::Always, false},
        {"Nodal glow", W::Slider, "cymaticsGlow", "Line glow", kOwn, R::Always, false},
        {"Iridescence", W::Slider, "cymaticsIridescence", "Rainbow sheen", kOwn, R::Always, false},
        {"Caustic sheen", W::Slider, "cymaticsCaustic", "Light ripples", kOwn, R::Always, false},
        {"Blend preset changes", W::Check, "milkdropBlendPresets", nullptr, kOwn, R::Always, false},
        {"MilkDrop palette tint", W::Slider, "milkdropPaletteTint", "Tint toward the palette", kOwn, R::Always, false},
        // Panel pieces. A piece's label is a PanelText id: the words Pro and the screen reader name it by.
        {"@macros", W::Piece, "", "", kOwn, R::Always, false},
        {"@macro_editor", W::Piece, "", "macro_editor_title", kOwn, R::Always, false},
        {"@palette_maker", W::Piece, "", "customize_your_colours", kOwn, R::Unsearched, false},
        {"@moments", W::Piece, "", "customize_moments", kOwn, R::Always, false},
        {"@rise_fall", W::Piece, "", "", kOwn, R::Always, false},
        {"@notes_key", W::Piece, "", "", kOwn, R::Always, false},
        {"@transition_style", W::Piece, "", "", kOwn, R::Always, false},
        {"@transition_length", W::Piece, "", "customize_transition_length", kSeconds, R::Transition, false},
        {"@reactions", W::Piece, "", "customize_reactions", kOwn, R::Always, false},
        {"@envelopes", W::Piece, "", "customize_envelopes", kOwn, R::Unsearched, false},
        {"@waves", W::Piece, "", "customize_waves", kOwn, R::Unsearched, false},
        {"@layers", W::Piece, "", "customize_layers", kOwn, R::Always, false},
        {"@shader_code", W::Piece, "", "customize_shader_code", kOwn, R::Unsearched, false},
    };
    return kControls;
}

const Control* find(std::string_view key) {
    for (const auto& c : table()) {
        if (key == c.key) return &c;
    }
    return nullptr;
}

std::string num(float v) { return tableNumber(v); }

const SceneParams& defaults() {
    static const SceneParams kDefaults;
    return kDefaults;
}

// A row's free text: a '|' or a line end would break it.
std::string cell(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

const char* labelOf(const Control& c) { return c.label ? c.label : c.key; }

bool ruleHolds(const Control& c, const SceneParams& p, const RowContext& ctx) {
    switch (c.rule) {
        case Rule::Always: return true;
        case Rule::CatchPoints: return p.get("fluidCatchPoints") > 0.0f;
        case Rule::DiveOrLiquid: return p.get("endlessZoom") != 0.0f || p.get("flowEnabled") != 0.0f;
        case Rule::Liquid: return p.get("flowEnabled") != 0.0f;
        case Rule::FluidStyle: return ctx.styleId.empty() || catalogue::has(catalogue::Trait::Fluid, ctx.styleId);
        case Rule::FluidGlow: return p.get("fluidBloom") != 0.0f;
        case Rule::Sunrays: return p.get("fluidSunrays") != 0.0f;
        case Rule::Unsearched: return ctx.query.find_first_not_of(" \t") == std::string::npos;
        case Rule::Transition: return ctx.transition;
    }
    return true;
}

// Whether a control reaches the style: Dive speed with Liquid on reaches every style, since Liquid dives on any.
bool reachesHere(const Control& c, const SceneParams& p, const RowContext& ctx) {
    if (c.rule == Rule::DiveOrLiquid && p.get("flowEnabled") != 0.0f) return true;
    return reaches(c.key, ctx.styleId);
}

int clampedChoice(const SceneParams& p, const char* field, size_t count) {
    const int at = static_cast<int>(std::lround(p.get(field)));
    return std::clamp(at, 0, std::max(static_cast<int>(count) - 1, 0));
}

void choiceRow(std::string& out, const char* type, const Control& c, const SceneParams& p) {
    const int slot = SceneParams::slotOf(c.field);
    if (slot < 0) return;
    const auto& labels = SceneParams::choices(static_cast<ParamSlot>(slot));
    if (labels.empty()) return;
    out += std::string(type) + "|" + c.key + "|" + c.field + "|" + labelOf(c) + "|" + std::to_string(clampedChoice(p, c.field, labels.size()));
    for (const auto& label : labels) out += "|" + cell(label);
    out += '\n';
}

void sliderRow(std::string& out, const Control& c, const SceneParams& p) {
    control::Range range{};
    if (!control::of(c.field, range)) return;
    // A whole-fold control stops on each count by hand, though a knob or a wave still moves it smoothly.
    const int steps = std::string_view(c.field) == "kaleidoFolds"
                          ? slider::intSteps(static_cast<int>(range.min), static_cast<int>(range.max))
                          : 0;
    out += std::string("SLIDER|") + c.key + "|" + c.field + "|" + labelOf(c) + "|" + num(p.get(c.field)) + "|" + num(range.min) + "|" +
           num(range.max) + "|" + (c.noDefault ? std::string() : num(defaults().get(c.field))) + "|" + std::to_string(c.unit) + "|" +
           std::to_string(steps) + "\n";
}

void intRow(std::string& out, const Control& c, const SceneParams& p) {
    control::Range range{};
    if (!control::of(c.field, range)) return;
    out += std::string("INT|") + c.key + "|" + c.field + "|" + labelOf(c) + "|" + std::to_string(std::lround(p.get(c.field))) + "|" +
           std::to_string(static_cast<int>(range.min)) + "|" + std::to_string(static_cast<int>(range.max)) + "\n";
}

void checkRow(std::string& out, const Control& c, const SceneParams& p) {
    out += std::string("CHECK|") + c.key + "|" + c.field + "|" + labelOf(c) + "|" + (p.get(c.field) != 0.0f ? "1" : "0") + "\n";
}

void pieceRow(std::string& out, std::string_view id, std::string_view lockKey, std::string_view label, std::string_view text) {
    out += "PIECE|" + std::string(id) + "|" + std::string(lockKey) + "|" + cell(label) + "|" + cell(text) + "\n";
}

void piece(std::string& out, const Control& c, const RowContext& ctx) {
    const std::string_view id = c.key;
    if (id == "Reaction DNA") {
        if (!matches(ctx.query, c.key) && !matches(ctx.query, panel::text(c.label))) return;
        pieceRow(out, "@personality", c.key, panel::text(c.label), panel::text("customize_new_personality"));
    } else if (id == "@rise_fall") {
        // How fast the picture follows a sound up and lets it go.
        out += std::string("SHELL|Rise|ATTACK|") + panel::text("customize_rise") + "|0.05|1|-1\n";
        out += std::string("SHELL|Fall|DECAY|") + panel::text("customize_fall") + "|0.02|0.6|-1\n";
    } else if (id == "@transition_length") {
        out += std::string("SHELL|Transition length|TRANSITION|") + panel::text(c.label) + "|0.3|5|" + std::to_string(c.unit) + "\n";
    } else if (id == "@waves") {
        pieceRow(out, id, id, panel::text(c.label), panel::text("customize_randomize_waves"));
    } else {
        pieceRow(out, id, id, panel::text(c.label), "");
    }
}

void palette(std::string& out, const Control& c, const RowContext& ctx) {
    if (ctx.query.find_first_not_of(" \t") == std::string::npos) {
        const char* take = panel::text("customize_palette_from_artwork");
        pieceRow(out, "@art_palette", c.key, take, take);
    }
    if (!matches(ctx.query, c.key)) return;
    // The stack draws on the shader styles; elsewhere the look's own palette is one list.
    const bool stack = ctx.styleId.empty() || catalogue::reaches(catalogue::Scope::ShaderLook, ctx.styleId);
    pieceRow(out, stack ? "@palette_stack" : "@palette_list", c.key, c.key, "");
}

void geometry(std::string& out, const Control& c, const SceneParams& p, const RowContext& ctx) {
    if (!ctx.styleId.empty() && !catalogue::has(catalogue::Trait::Cymatics, ctx.styleId)) return;
    if (const auto held = catalogue::overrideOf(ctx.styleId, c.key)) {
        const int slot = SceneParams::slotOf(c.field);
        if (slot < 0) return;
        const auto& shapes = SceneParams::choices(static_cast<ParamSlot>(slot));
        const int at = static_cast<int>(std::lround(*held));
        if (at >= 0 && at < static_cast<int>(shapes.size())) {
            out += std::string("HINT|") + c.key + "|" + cell(panel::format("customize_shape_held", {std::string(shapes[static_cast<size_t>(at)])})) + "\n";
            return;
        }
    }
    choiceRow(out, "CHIPS", c, p);
}

void liquid(std::string& out, const Control& c, const SceneParams& p, const RowContext& ctx) {
    checkRow(out, c, p);
    const char* note = ctx.liquidMode == 2 || ctx.liquidMode == 3 ? panel::text("customize_liquid_hot_simplified")
                       : ctx.liquidMode == 4                     ? panel::text("customize_liquid_hot_paused")
                                                                 : "";
    if (*note) out += std::string("NOTE|") + c.key + "|" + note + "\n";
}

}  // namespace

const std::vector<Control>& controls() { return table(); }

bool matches(std::string_view query, std::string_view text) {
    const size_t first = query.find_first_not_of(" \t");
    if (first == std::string_view::npos) return true;
    const size_t last = query.find_last_not_of(" \t");
    const std::string_view q = query.substr(first, last - first + 1);
    const auto lower = [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); };
    const auto it = std::search(text.begin(), text.end(), q.begin(), q.end(), [&](char a, char b) { return lower(a) == lower(b); });
    return it != text.end();
}

std::string rows(std::string_view keys, const SceneParams& p, const RowContext& ctx) {
    std::string out = "rows|" + std::to_string(kRowsVersion) + "\n";
    size_t start = 0;
    while (start < keys.size()) {
        const size_t end = std::min(keys.find('\n', start), keys.size());
        const std::string_view key = keys.substr(start, end - start);
        start = end + 1;
        const Control* c = find(key);
        // A name the map has and this table does not: nothing, rather than a wrong widget.
        if (!c || !reachesHere(*c, p, ctx) || !ruleHolds(*c, p, ctx)) continue;
        switch (c->widget) {
            case Widget::Slider: sliderRow(out, *c, p); break;
            case Widget::IntSlider: intRow(out, *c, p); break;
            case Widget::Check: checkRow(out, *c, p); break;
            case Widget::Chips: choiceRow(out, "CHIPS", *c, p); break;
            case Widget::Pick: choiceRow(out, "PICK", *c, p); break;
            case Widget::Palette: palette(out, *c, ctx); break;
            case Widget::Geometry: geometry(out, *c, p, ctx); break;
            case Widget::Liquid: liquid(out, *c, p, ctx); break;
            case Widget::Piece: piece(out, *c, ctx); break;
        }
    }
    return out;
}

}  // namespace geode::viz::customize
