// Host-side checks for the scene params schema (core/viz/ParamTable.inc, expanded by core/viz/Params and
// core/viz/ParamsFields): one row per field gives its name, kind, default, range and wire slot, and the
// struct, the wire, get/set, lerpParams, the LFO clamps and the shell's text table all come from it.
// Built and run by CI (.github/workflows/android.yml, "Native host tests") with the system compiler; no GL.
//
//   params_schema_test core/tests/params_schema.txt           checks, including schemaText() against the fixture
//   params_schema_test --write core/tests/params_schema.txt   rewrites the fixture after a deliberate change
//
// The fixture is what the shell's JVM tests read in place of geode_params_schema_text (the geode.paramsSchema
// property, dev.geode.render.scene.ParamSchema), so this check is what keeps their schema the device's.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Params.hpp"
#include "viz/fluid/FluidQuality.hpp"
#include "viz/scenes/CymaticsMath.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// The geode_viz_set_params frame size. Kotlin's SceneParamsCodec.FIELDS says the same number; a row
// added to the table changes both, never one.
constexpr int kWireFloats = 189;

bool integral(float v) { return std::floor(v) == v; }

void theTableIsTheWire() {
    check(SceneParams::kFieldCount == kWireFloats, "the table has one row per wire float");
    check(static_cast<int>(SceneParams::schema().size()) == kWireFloats, "schema() has every row");
    check(static_cast<int>(SceneParams::fieldNames().size()) == kWireFloats, "fieldNames() has every row");
    std::set<std::string> seen;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        const std::string name = s.name;
        check(!name.empty(), "slot " + std::to_string(slot) + " has a name");
        check(seen.insert(name).second, name + " is named once");
        check(std::string(SceneParams::fieldNames()[static_cast<size_t>(slot)]) == name, name + ": fieldNames() is the schema's order");
        check(SceneParams::slotOf(name) == slot, name + " sits in slot " + std::to_string(slot));
    }
    check(SceneParams::slotOf("noSuchField") == -1, "an unknown name has no slot");
    // The slot enum names the same rows: the first, a middle one and the last.
    check(SceneParams::spec(ParamSlot::speed).name == std::string("speed"), "ParamSlot::speed is slot 0");
    check(static_cast<int>(ParamSlot::speed) == 0, "speed is the wire's first float");
    check(SceneParams::spec(ParamSlot::flowCurl).name == std::string("flowCurl"), "ParamSlot::flowCurl names flowCurl");
    check(static_cast<int>(ParamSlot::camFlat) == 163, "camFlat keeps its slot");
    check(static_cast<int>(ParamSlot::paletteStop4) == 168, "paletteStop4 keeps its slot");
    check(static_cast<int>(ParamSlot::diveFollow) == 177, "diveFollow keeps its slot");
    check(static_cast<int>(ParamSlot::paletteAmount0) == 178 && static_cast<int>(ParamSlot::paletteAmount7) == 185,
          "the palette amounts follow it, the primary first");
    check(static_cast<int>(ParamSlot::paletteShift) == 186, "paletteShift keeps its slot");
    check(static_cast<int>(ParamSlot::kaleidoFolds) == 187, "kaleidoFolds keeps its slot");
    check(static_cast<int>(ParamSlot::layerMix) == kWireFloats - 1, "layerMix is the wire's last float");
}

void everyRowIsWellFormed() {
    const SceneParams defaults{};
    for (const ParamSpec& s : SceneParams::schema()) {
        const std::string name = s.name;
        check(s.min <= s.max, name + ": min <= max");
        check(s.defaultValue >= s.min && s.defaultValue <= s.max, name + ": the default lies within [min, max]");
        check(defaults.get(name) == s.defaultValue, name + ": SceneParams{} holds the row's default");
        if (s.kind == ParamKind::Int || s.kind == ParamKind::Bool) {
            check(integral(s.min) && integral(s.max) && integral(s.defaultValue), name + ": a count has whole bounds");
            check(s.fade == ParamFade::Hold, name + ": a count snaps, never glides");
        }
        if (s.kind == ParamKind::Bool) check(s.min == 0.0f && s.max == 1.0f, name + ": a flag is 0..1");
    }
}

// Values for one row: both ends and a point inside, each something the field can hold exactly.
std::vector<float> probes(const ParamSpec& s) {
    if (s.kind == ParamKind::Bool) return {0.0f, 1.0f};
    if (s.kind == ParamKind::Int) return {s.min, s.max, std::round((s.min + s.max) * 0.5f)};
    return {s.min, s.max, s.min + (s.max - s.min) * 0.375f};
}

void getAndSetRoundTripByNameAndSlot() {
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        const std::string name = s.name;
        if (SceneParams::retired(static_cast<ParamSlot>(slot))) continue;  // anOldLookWithRetiredNamesStillLoads
        for (float v : probes(s)) {
            SceneParams p;
            check(p.set(name, v), name + ": set by name knows it");
            check(p.get(name) == v, name + ": get(set(" + std::to_string(v) + ")) by name");
            check(p.getSlot(slot) == v, name + ": the slot reads what the name wrote");
            SceneParams q;
            check(q.setSlot(slot, v), name + ": set by slot");
            check(q.get(name) == v, name + ": the name reads what the slot wrote");
            // Every other field keeps its default.
            for (int other = 0; other < SceneParams::kFieldCount; ++other) {
                if (other != slot && q.getSlot(other) != SceneParams::schema()[static_cast<size_t>(other)].defaultValue) {
                    check(false, name + ": writing it moved " + SceneParams::schema()[static_cast<size_t>(other)].name);
                }
            }
        }
    }
    SceneParams p;
    check(!p.set("noSuchField", 1.0f), "set refuses an unknown name");
    check(p.get("noSuchField") == 0.0f, "get of an unknown name is 0");
    check(!p.setSlot(-1, 1.0f) && !p.setSlot(kWireFloats, 1.0f), "setSlot refuses a slot off the wire");
    check(p.getSlot(kWireFloats) == 0.0f, "getSlot off the wire is 0");
    // Ints round and flags are > 0.5, as the wire always read them.
    p.set("fluidIterations", 12.6f);
    check(p.fluidIterations == 13, "an int rounds");
    p.set("trails", 0.4f);
    check(!p.trails, "0.4 is off");
    p.set("trails", 0.6f);
    check(p.trails, "0.6 is on");
}

// A look saved before the rows were retired names them still (and a later build's look may name rows this
// one has never heard of). Loading it by name, the way the bundled looks and Kotlin's preset reader do,
// keeps every live value, drops the rest and moves no slot.
void anOldLookWithRetiredNamesStillLoads() {
    const std::pair<const char*, float> kOldLook[] = {
        {"speed", 1.4f},     {"pulse", 0.6f},       {"shake", 0.4f},  {"flash", 0.6f},        {"strobe", 0.5f},
        {"glitch", 0.75f},   {"formDrive", 0.2f},   {"bloom", 0.5f},  {"motionOrbit", 0.9f},  {"beatFlash", 0.5f},
        {"motionDrift", 0.8f},  {"waterDamping", 0.95f}, {"rippleOverlayEnabled", 1.0f},
        {"beatResponse", 1.9f}, {"symmetry", 8.0f},  {"kaleidoscope", 1.0f},
    };
    const SceneParams fresh;
    SceneParams p;
    for (const auto& [name, value] : kOldLook) {
        const int slot = SceneParams::slotOf(name);
        const bool dead = slot < 0 || SceneParams::retired(static_cast<ParamSlot>(slot));
        check(p.set(name, value) == !dead, std::string(name) + (dead ? " is dropped" : " is read"));
    }
    check(p.speed == 1.4f && p.bloom == 0.5f && p.motionDrift == 0.8f, "the live values land");
    check(static_cast<int>(ParamSlot::pulse) == 6 && static_cast<int>(ParamSlot::strobe) > static_cast<int>(ParamSlot::flash),
          "a retired row keeps its slot");
    for (const ParamSlot slot : kRetiredParams) {
        const int i = static_cast<int>(slot);
        check(p.getSlot(i) == fresh.getSlot(i), std::string(SceneParams::spec(slot).name) + " stays at its default");
        check(SceneParams::slotOf(SceneParams::spec(slot).name) == i, "and is still a known name");
    }
    // The same over the wire: an old shell's frame carrying values in the retired slots.
    std::vector<float> wire(kWireFloats);
    p.toWire(wire.data());
    for (const ParamSlot slot : kRetiredParams) wire[static_cast<size_t>(slot)] = 0.9f;
    const SceneParams q = SceneParams::fromWire(wire.data());
    std::vector<float> a(kWireFloats);
    std::vector<float> b(kWireFloats);
    p.toWire(a.data());
    q.toWire(b.data());
    check(a == b, "a retired slot's wire value is ignored");
    check(SceneParams::retired(ParamSlot::beatResponse) && static_cast<int>(ParamSlot::beatResponse) == 11,
          "beatResponse is retired and keeps slot 11");
}

void theWireRoundTrips() {
    SceneParams p;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        p.setSlot(slot, probes(SceneParams::schema()[static_cast<size_t>(slot)]).back());  // a retired slot keeps its default
    }
    std::vector<float> wire(kWireFloats);
    p.toWire(wire.data());
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        check(wire[static_cast<size_t>(slot)] == p.getSlot(slot), std::string(SceneParams::schema()[static_cast<size_t>(slot)].name) + " lands in its slot");
    }
    std::vector<float> again(kWireFloats);
    SceneParams::fromWire(wire.data()).toWire(again.data());
    check(again == wire, "fromWire(toWire(p)) == p");
}

// Kills storing a wire value as it came: a NaN or an infinity kept in SceneParams stays in the resolver's
// lerp for good (and lround of one is undefined), and an int far off its row overflows the round.
void aBrokenWireKeepsTheDefaults() {
    std::vector<float> defaults(kWireFloats);
    SceneParams{}.toWire(defaults.data());
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), inf, -inf}) {
        const std::vector<float> wire(kWireFloats, bad);
        std::vector<float> got(kWireFloats);
        SceneParams::fromWire(wire.data()).toWire(got.data());
        check(got == defaults, "a wire of " + std::to_string(bad) + " yields the defaults");
    }
    std::vector<float> wire = defaults;
    wire[static_cast<size_t>(ParamSlot::speed)] = 2.5f;
    wire[static_cast<size_t>(ParamSlot::zoom)] = inf;
    wire[static_cast<size_t>(ParamSlot::fluidBeatSplats)] = 1e30f;
    wire[static_cast<size_t>(ParamSlot::fluidQuality)] = -1e30f;
    wire[static_cast<size_t>(ParamSlot::fluidIterations)] = 12.6f;
    const SceneParams p = SceneParams::fromWire(wire.data());
    check(p.speed == 2.5f && p.zoom == SceneParams{}.zoom, "one bad float leaves its neighbours alone");
    check(p.fluidBeatSplats == static_cast<int>(SceneParams::spec(ParamSlot::fluidBeatSplats).max), "an int over its row is held to the row's max");
    check(p.fluidQuality == static_cast<int>(SceneParams::spec(ParamSlot::fluidQuality).min), "an int under its row is held to the row's min");
    check(p.fluidIterations == 13, "an int in range still rounds");
}

void fadeGlidesAndHoldSnaps() {
    SceneParams from;
    SceneParams to;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        from.setSlot(slot, s.min);
        to.setSlot(slot, s.max);
    }
    const SceneParams half = lerpParams(from, to, 0.5f);
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        if (SceneParams::retired(static_cast<ParamSlot>(slot))) continue;
        const float want = s.fade == ParamFade::Fade ? s.min + (s.max - s.min) * 0.5f : s.max;
        check(std::fabs(half.getSlot(slot) - want) <= 1e-6f * std::fmax(1.0f, std::fabs(want)),
              std::string(s.name) + (s.fade == ParamFade::Fade ? " glides halfway" : " snaps to the new value"));
    }
    // The NOT_FADED sentinels: a glide through -1 would flash the palette's own colours.
    for (const char* held : {"paletteBaseOverride", "paletteRangeOverride", "palette2BaseOverride", "palette2RangeOverride", "paramFadeSec"}) {
        check(SceneParams::schema()[static_cast<size_t>(SceneParams::slotOf(held))].fade == ParamFade::Hold, std::string(held) + " is held");
    }
}

// The clamps every LFO target had before the table owned them (Lfo.cpp, #346): the table's range must
// keep each one, so moving the clamp into the table changed no modulation.
struct OldClamp {
    LfoTarget target;
    const char* field;
    float lo;
    float hi;
};
constexpr OldClamp kOldClamps[] = {
    {LfoTarget::Speed, "speed", 0.05f, 4.0f},           {LfoTarget::Zoom, "zoom", 0.3f, 3.0f},
    {LfoTarget::Rotation, "rotation", -3.0f, 3.0f},     {LfoTarget::Sway, "sway", 0.0f, 1.0f},
    {LfoTarget::DriftX, "driftX", -1.0f, 1.0f},
    {LfoTarget::DriftY, "driftY", -1.0f, 1.0f},         {LfoTarget::Warp, "warp", 0.0f, 1.0f},
    {LfoTarget::Ripple, "ripple", 0.0f, 1.0f},          {LfoTarget::Morph, "morph", 0.0f, 1.0f},
    {LfoTarget::Twist, "twist", -1.0f, 1.0f},           {LfoTarget::Tile, "tile", 1.0f, 6.0f},
    {LfoTarget::Pixelate, "pixelate", 0.0f, 1.0f},      {LfoTarget::Posterize, "posterize", 0.0f, 1.0f},
    {LfoTarget::PaletteMix, "paletteAmount1", 0.0f, 1.0f},  {LfoTarget::Saturation, "saturation", 0.0f, 1.5f},
    {LfoTarget::Brightness, "brightness", 0.2f, 2.0f},  {LfoTarget::Intensity, "intensity", 0.2f, 2.0f},
    {LfoTarget::Bloom, "bloom", 0.0f, 1.0f},            {LfoTarget::Temperature, "temperature", -1.0f, 1.0f},
    {LfoTarget::Turbulence, "turbulence", 0.0f, 1.5f},  {LfoTarget::ChromaAb, "chromaAb", 0.0f, 1.0f},
    {LfoTarget::Vignette, "vignette", 0.0f, 1.0f},      {LfoTarget::Fisheye, "fisheye", -1.0f, 1.0f},
    {LfoTarget::ParticleSize, "particleSize", 0.3f, 2.5f}, {LfoTarget::TrailLength, "trailLength", 0.05f, 0.98f},
    {LfoTarget::FluidCurl, "fluidCurl", 0.0f, 50.0f},   {LfoTarget::FluidRadius, "fluidSplatRadius", 0.02f, 0.4f},
    {LfoTarget::FluidForce, "fluidSplatForce", 0.0f, 3.0f}, {LfoTarget::FluidGlow, "fluidBloomIntensity", 0.1f, 2.0f},
    {LfoTarget::FluidFade, "fluidDensityDissipation", 0.0f, 4.0f}, {LfoTarget::FluidCatchPull, "fluidCatchPull", 0.0f, 3.0f},
    {LfoTarget::FluidCatchRadius, "fluidCatchRadius", 0.03f, 0.3f}, {LfoTarget::FlowStrength, "flowStrength", 0.0f, 1.0f},
    {LfoTarget::CamFly, "camFly", -2.5f, 2.5f},          {LfoTarget::CamOrbit, "camOrbit", 0.0f, 1.0f},
    {LfoTarget::CamPush, "camPush", -1.0f, 1.0f},       {LfoTarget::CamYaw, "camYaw", -1.0f, 1.0f},
    {LfoTarget::CamPitch, "camPitch", -1.0f, 1.0f},     {LfoTarget::CamRoll, "camRoll", -1.0f, 1.0f},
    {LfoTarget::CamFov, "camFov", 0.5f, 2.0f},          {LfoTarget::CamShake, "camShake", 0.0f, 1.0f},
    {LfoTarget::FlowCurl, "flowCurl", 0.0f, 50.0f},     {LfoTarget::FlowForce, "flowForce", 0.0f, 3.0f},
    {LfoTarget::EndlessZoomSpeed, "endlessZoomSpeed", -1.2f, 1.2f},
    {LfoTarget::DiveTwist, "diveTwist", -1.0f, 1.0f},   {LfoTarget::DiveAimX, "diveAimX", -0.6f, 0.6f},
    {LfoTarget::DiveAimY, "diveAimY", -0.6f, 0.6f},
};

void theLfoClampsAreTheTablesRanges() {
    const SceneParams base{};
    for (const OldClamp& c : kOldClamps) {
        const std::string name = c.field;
        const SceneParams up = applyLfoTarget(base, c.target, 1000.0f);
        const SceneParams down = applyLfoTarget(base, c.target, -1000.0f);
        check(up.get(name) == c.hi, name + ": an LFO pushed up stops at " + std::to_string(c.hi));
        check(down.get(name) == c.lo, name + ": an LFO pushed down stops at " + std::to_string(c.lo));
        // Nothing else moves.
        for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
            if (slot == SceneParams::slotOf(name)) continue;
            if (up.getSlot(slot) != base.getSlot(slot)) check(false, name + "'s LFO moved " + SceneParams::schema()[static_cast<size_t>(slot)].name);
        }
    }
    // The palette stack's amounts stop at 0..1; its position is Circular, like the hue.
    check(applyLfoTarget(base, LfoTarget::PaletteAmount1, 1000.0f).paletteAmount0 == 1.0f &&
              applyLfoTarget(base, LfoTarget::PaletteAmount8, -1000.0f).paletteAmount7 == 0.0f,
          "a palette amount stops at its range");
    check(SceneParams::spec(ParamSlot::paletteShift).kind == ParamKind::Circular &&
              applyLfoTarget(base, LfoTarget::PaletteShift, 3.0f).paletteShift == base.paletteShift + 1.5f,
          "the palette position is never clamped");
    // The kaleidoscope's folds: 8 per unit, held to Off (1) .. 16.
    check(applyLfoTarget(base, LfoTarget::KaleidoFolds, 0.5f).kaleidoFolds == base.kaleidoFolds + 4.0f &&
              applyLfoTarget(base, LfoTarget::KaleidoFolds, 1000.0f).kaleidoFolds == 16.0f &&
              applyLfoTarget(base, LfoTarget::KaleidoFolds, -1000.0f).kaleidoFolds == 1.0f,
          "a wave on the kaleidoscope moves 8 folds per unit, from off to 16");
    // The hue is Circular: it adds unclamped, at half a turn per unit, as it always did.
    check(SceneParams::spec(ParamSlot::colorShift).kind == ParamKind::Circular, "colorShift is circular");
    check(applyLfoTarget(base, LfoTarget::ColorShift, 3.0f).colorShift == base.colorShift + 1.5f, "the hue LFO is never clamped");
    // Scaled targets keep their scale inside the range.
    check(applyLfoTarget(base, LfoTarget::FlowCurl, 0.2f).flowCurl == base.flowCurl + 0.2f * 25.0f, "Swirl moves 25 per unit");
    check(applyLfoTarget(base, LfoTarget::EndlessZoomSpeed, 0.2f).endlessZoomSpeed == base.endlessZoomSpeed + 0.2f * 0.5f,
          "Dive moves 0.5 per unit");
    // Targets that write no scene parameter leave the params alone.
    std::vector<float> before(kWireFloats);
    std::vector<float> after(kWireFloats);
    base.toWire(before.data());
    for (LfoTarget t : {LfoTarget::None, LfoTarget::Retired5, LfoTarget::Retired25, LfoTarget::Retired37, LfoTarget::Retired38, LfoTarget::Lfo1Rate,
                        LfoTarget::Lfo3Depth}) {
        applyLfoTarget(base, t, 1.0f).toWire(after.data());
        check(after == before, "a target with no scene field changes nothing");
    }
}

void theCataloguesRangesAgree() {
    const auto same = [](ParamSlot slot, float lo, float hi, const char* what) {
        const ParamSpec& s = SceneParams::spec(slot);
        check(s.min == lo && s.max == hi, std::string(s.name) + " has the catalogue's " + what + " range");
    };
    same(ParamSlot::marchDetail, catalogue::kMarchDetail.min, catalogue::kMarchDetail.max, "Detail");
    same(ParamSlot::zoom, catalogue::kTouchZoom.min, catalogue::kTouchZoom.max, "Zoom");
    same(ParamSlot::rotation, catalogue::kTouchRotation.min, catalogue::kTouchRotation.max, "Rotation");
    same(ParamSlot::cymaticsFundamental, cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz, "Fundamental (Hz)");
    same(ParamSlot::cymaticsModes, 1.0f, static_cast<float>(cymatics::kMaxRenderedModes), "Standing waves");
}

// A picker's labels (its choices row) name every value of its range once, and travel in the text as they are.
void everyPickerLabelsItsRange() {
    int pickers = 0;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const auto p = static_cast<ParamSlot>(slot);
        const auto& labels = SceneParams::choices(p);
        const std::string name = SceneParams::spec(p).name;
        check(static_cast<int>(labels.size()) == choiceCount(p), name + ": choices() and choiceCount() agree");
        if (labels.empty()) continue;
        ++pickers;
        const ParamSpec& s = SceneParams::spec(p);
        check(!SceneParams::retired(p) && s.kind == ParamKind::Int, name + ": a live Int picker");
        check(s.min == 0.0f && s.max == static_cast<float>(labels.size() - 1), name + ": one label per value of 0..max");
        std::set<std::string_view> seen;
        for (std::string_view l : labels) {
            check(!l.empty() && l.find_first_of("|\n") == std::string_view::npos, name + ": a label the text can carry");
            check(seen.insert(l).second, name + ": each label once");
        }
    }
    check(pickers == 9, "nine pickers carry labels");
    const auto& paths = SceneParams::choices(ParamSlot::camPath);
    check(paths.size() == 12 && paths.front() == "Glide" && paths.back() == "Pendulum", "the camera paths in CameraRig's order");
    const auto& tiers = SceneParams::choices(ParamSlot::fluidQuality);
    for (size_t i = 0; i < tiers.size() && i < fluid::quality::kTiers.size(); ++i) {
        check(tiers[i] == fluid::quality::kTiers[i].label, "fluid quality " + std::to_string(i) + " is its tier's label");
    }
    check(SceneParams::choices(ParamSlot::speed).empty() && SceneParams::choices(ParamSlot::kCount).empty(), "a slider or no slot has none");
}

// The kaleidoscope is one slider, kaleidoFolds: 1 is off, 2..16 the folds. The shaders fold by its nearest whole count,
// a half up, so a wave steps from count to count; off they read the default count. A roll deals real folds in range,
// and a look saved with the retired switch and count opens as it was.
void theKaleidoscopeIsOneSlider() {
    const ParamSpec& s = SceneParams::spec(ParamSlot::kaleidoFolds);
    check(s.kind == ParamKind::Float && s.defaultValue == 1.0f && s.min == 1.0f && s.max == 16.0f, "kaleidoFolds runs 1 (off) .. 16, off by default");
    check(SceneParams::retired(ParamSlot::symmetry) && SceneParams::retired(ParamSlot::kaleidoscope), "the old switch and count are retired");
    const auto& folds = SceneParams::kSymmetryFolds;
    check(folds.front() == 2 && static_cast<float>(folds.back()) == s.max, "a roll deals from 2 to the slider's max");
    for (size_t i = 1; i < folds.size(); ++i) check(folds[i] > folds[i - 1], "the folds rise: " + std::to_string(folds[i]));
    check(SceneParams::kaleidoFoldCount(1.0f) == 1 && SceneParams::kaleidoFoldCount(1.49f) == 1 && SceneParams::kaleidoFoldCount(1.5f) == 2,
          "below 1.5 the kaleidoscope is off; a half rounds up");
    check(SceneParams::kaleidoFoldCount(7.4f) == 7 && SceneParams::kaleidoFoldCount(7.5f) == 8 && SceneParams::kaleidoFoldCount(16.0f) == 16,
          "the nearest whole count");
    check(SceneParams::kaleidoFoldCount(-3.0f) == 1 && SceneParams::kaleidoFoldCount(99.0f) == 16 &&
              SceneParams::kaleidoFoldCount(std::numeric_limits<float>::quiet_NaN()) == 1,
          "held to 1..16, NaN off");
    const auto off = SceneParams::kaleidoUniforms(1.2f);
    check(off.on == 0.0f && off.folds == static_cast<float>(SceneParams::kDefaultSymmetryFolds), "off: no fold, the default count for styles that read one");
    const auto on = SceneParams::kaleidoUniforms(5.6f);
    check(on.on == 1.0f && on.folds == 6.0f, "on: the whole count");
    check(SceneParams::kaleidoUniforms(SceneParams{}.kaleidoFolds).on == 0.0f, "a fresh look has no kaleidoscope");
    check(SceneParams::kaleidoFoldsOf(false, 8) == 1.0f && SceneParams::kaleidoFoldsOf(false, 0) == 1.0f, "an old look switched off is off");
    check(SceneParams::kaleidoFoldsOf(true, 8) == 8.0f && SceneParams::kaleidoFoldsOf(true, 0) == 2.0f && SceneParams::kaleidoFoldsOf(true, 40) == 16.0f,
          "an old look switched on keeps its count, held to 2..16");
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '|')) out.push_back(field);
    return out;
}

void theTextIsTheTable() {
    std::istringstream lines(SceneParams::schemaText());
    std::string line;
    std::getline(lines, line);
    check(line == "params|" + std::to_string(SceneParams::kSchemaTextVersion) + "|" + std::to_string(kWireFloats),
          "the text starts with its version and row count: " + line);
    int slot = 0;
    size_t palette = 0;
    size_t choiceRows = 0;
    while (std::getline(lines, line)) {
        const auto f = split(line);
        if (!f.empty() && f[0] == "choice") {
            const int at = f.size() == 4 ? SceneParams::slotOf(f[1]) : -1;
            const auto& labels = SceneParams::choices(static_cast<ParamSlot>(at < 0 ? SceneParams::kFieldCount : at));
            const size_t index = f.size() == 4 ? std::strtoul(f[2].c_str(), nullptr, 10) : labels.size();
            check(palette == static_cast<size_t>(SceneParams::kListedPalettes), "the choice rows follow the palette names: " + line);
            check(index < labels.size() && f[3] == labels[index], "a choice row names the picker's label at its index: " + line);
            ++choiceRows;
            continue;
        }
        if (!f.empty() && f[0] == "palette") {
            const auto& names = SceneParams::palettes();
            check(slot == kWireFloats, "the palette names follow every param row: " + line);
            check(f.size() == 3 && f[1] == std::to_string(palette) && palette < names.size() && f[2] == names[palette].name,
                  "a palette row names the built-in at its index: " + line);
            ++palette;
            continue;
        }
        if (slot >= SceneParams::kFieldCount) {
            check(false, "a row past the wire: " + line);
            break;
        }
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        if (f.size() != 9) {
            check(false, "nine fields: " + line);
            ++slot;
            continue;
        }
        static const char* kKinds[] = {"FLOAT", "INT", "BOOL", "CIRCULAR"};
        check(f[0] == "param", "a param row: " + line);
        check(f[1] == std::to_string(slot), "rows come in slot order: " + line);
        check(f[2] == s.name, "the row names its field: " + line);
        check(f[3] == kKinds[static_cast<int>(s.kind)], "the row says its kind: " + line);
        check(std::strtof(f[4].c_str(), nullptr) == s.defaultValue, "the default reads back exactly: " + line);
        check(std::strtof(f[5].c_str(), nullptr) == s.min, "min reads back exactly: " + line);
        check(std::strtof(f[6].c_str(), nullptr) == s.max, "max reads back exactly: " + line);
        check(f[7] == (s.fade == ParamFade::Fade ? "FADE" : "HOLD"), "the row says FADE or HOLD: " + line);
        check(f[8] == (SceneParams::retired(static_cast<ParamSlot>(slot)) ? "RETIRED" : "LIVE"), "the row says LIVE or RETIRED: " + line);
        ++slot;
    }
    check(slot == kWireFloats, "one row per slot");
    check(palette == static_cast<size_t>(SceneParams::kListedPalettes), "one name per listed built-in palette, none for a retired one");
    size_t labels = 0;
    for (int s = 0; s < SceneParams::kFieldCount; ++s) labels += SceneParams::choices(static_cast<ParamSlot>(s)).size();
    check(choiceRows == labels, "one choice row per picker label");
    // The largest seed is a whole number on the wire, not "1.67772e+07".
    check(SceneParams::schemaText().find("|reactSeed|INT|0|0|16777215|HOLD|LIVE\n") != std::string::npos, "reactSeed's max is exact");
    check(SceneParams::schemaText().find("|fluidCatchRadius|FLOAT|0.12|0.03|0.3|FADE|LIVE\n") != std::string::npos, "short numbers stay short");
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::string out;
    for (char c; in.get(c);) {
        if (c != '\r') out += c;  // a Windows checkout
    }
    return out;
}

void textEqualsTheFixture(const char* path) {
    const std::string fixture = readFile(path);
    check(!fixture.empty(), std::string("fixture readable: ") + path);
    if (fixture == SceneParams::schemaText()) return;
    std::istringstream want(fixture);
    std::istringstream got(SceneParams::schemaText());
    for (int line = 1;; ++line) {
        std::string w;
        std::string g;
        const bool hasW = static_cast<bool>(std::getline(want, w));
        const bool hasG = static_cast<bool>(std::getline(got, g));
        if (!hasW && !hasG) break;
        if (!hasW) w = "<end>";
        if (!hasG) g = "<end>";
        if (w != g) {
            check(false, "schemaText() differs from " + std::string(path) + " at line " + std::to_string(line) + ": fixture [" + w +
                             "] native [" + g + "]; after a deliberate change run params_schema_test --write " + path);
            return;
        }
    }
    check(false, "schemaText() differs from " + std::string(path) + " only in its line ends; run params_schema_test --write " + path);
}

}  // namespace

// The palette a slot draws, pinned to the Kotlin SceneParams.PALETTES table and its paletteBase/paletteRange
// getters it replaces (the shell now asks geode_params_palette_base/_range): the override when >= 0, else
// the built-in palette, the index clamped into the 21 rows; and the table as long as the schema's range.
void palettesAreTheShellsOldTable() {
    struct Row {
        float base, range;
    };
    const Row kotlin[] = {{0.0f, 1.0f},   {0.5f, 0.45f},  {0.0f, 0.14f}, {0.5f, 0.2f},   {0.6f, 0.02f},  {0.85f, 0.5f},
                          {0.33f, 0.18f}, {0.45f, 0.7f},  {0.05f, 0.3f}, {0.55f, 0.15f}, {0.78f, 0.35f}, {0.25f, 0.25f},
                          {0.7f, 0.25f},  {0.93f, 0.12f}, {0.07f, 0.1f}, {0.4f, 0.12f},  {0.65f, 0.5f},  {0.97f, 0.08f},
                          {0.5f, 0.08f},  {0.833f, 0.08f}, {0.167f, 0.08f}};
    constexpr int kRows = static_cast<int>(sizeof(kotlin) / sizeof(kotlin[0]));
    check(static_cast<int>(SceneParams::palettes().size()) == kRows, "21 built-in palettes, as Kotlin listed");
    const ParamSpec& palette = SceneParams::spec(ParamSlot::palette);
    check(static_cast<int>(palette.max) == kRows - 1 && static_cast<int>(palette.min) == 0,
          "the palette field's range is the table's rows");
    auto at = [&](int palette, float baseOverride, float rangeOverride) {
        SceneParams p;
        p.palette = palette;
        p.paletteBaseOverride = baseOverride;
        p.paletteRangeOverride = rangeOverride;
        return Row{p.paletteBase(), p.paletteRange()};
    };
    for (int i = -3; i < kRows + 3; ++i) {
        const Row want = kotlin[i < 0 ? 0 : (i >= kRows ? kRows - 1 : i)];
        const Row got = at(i, SceneParams::kUnsetOverride, SceneParams::kUnsetOverride);
        check(got.base == want.base && got.range == want.range, "palette " + std::to_string(i) + " reads the Kotlin row, clamped into the table");
    }
    const Row custom = at(4, 0.31f, 0.0f);
    check(custom.base == 0.31f && custom.range == 0.0f, "an override >= 0 wins, 0 included");
    const Row half = at(4, 0.31f, -1.0f);
    check(half.base == 0.31f && half.range == 0.02f, "each override stands alone");
}

// The meta rows (#1005 PR 4a): every live row has one, a retired row none (ParamsFields.cpp also asserts
// it at compile time), the rows one control labels agree on its scope and are few enough for a roll's
// fields (randomizer::kMaxRollFields), and a roll range sits inside the field's own range.
void everyLiveRowHasOneMeta() {
    std::vector<std::string> labels;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const auto s = static_cast<ParamSlot>(slot);
        const ParamMeta& m = SceneParams::meta(s);
        const ParamSpec& spec = SceneParams::spec(s);
        const std::string field = spec.name;
        check(m.live == !SceneParams::retired(s), field + ": a live row has a meta row, a retired one none");
        check(m.label != nullptr, field + ": a label, \"\" for none");
        if (!m.live) {
            check(m.label[0] == '\0' && m.flags == 0u && m.rollLo == 0.0f && m.rollHi == 0.0f, field + ": a retired row's meta is empty");
            continue;
        }
        if (m.rollLo != 0.0f || m.rollHi != 0.0f) {
            check(m.rollLo < m.rollHi, field + ": a roll range runs low to high");
            check(m.rollLo >= spec.min && m.rollHi <= spec.max, field + ": the roll range is inside the field's range");
        }
        if (m.label[0] == '\0') continue;
        int shared = 0;
        for (int other = 0; other < SceneParams::kFieldCount; ++other) {
            const ParamMeta& o = SceneParams::meta(static_cast<ParamSlot>(other));
            if (!o.live || std::strcmp(o.label, m.label) != 0) continue;
            ++shared;
            check(o.scope == m.scope, field + ": the fields of " + m.label + " share its scope");
        }
        // A panel piece ("@notes_key") is drawn as a block and rolls nothing; a slider or a picker is rolled
        // as one RolledKey, which carries three fields at most.
        check(m.label[0] == '@' || shared <= static_cast<int>(randomizer::kMaxRollFields),
              std::string(m.label) + ": no more fields per control than a roll's RolledKey holds");
    }
    check(std::string(SceneParams::meta(ParamSlot::speed).label) == "Speed", "speed's control is Speed");
    check(SceneParams::meta(ParamSlot::speed).scope == catalogue::Scope::SceneClock, "Speed paces the scene clock");
    check(std::string(SceneParams::meta(ParamSlot::paletteBaseOverride).label) == "Palette", "a hue override belongs to Palette");
    check(std::string(SceneParams::meta(ParamSlot::paletteStop4).label) == "Palette", "your colours' stops belong to Palette");
    check(!SceneParams::meta(ParamSlot::beatResponse).live, "beatResponse is retired: no meta, no control");
    check((SceneParams::meta(ParamSlot::camYaw).flags & kMod) != 0 && (SceneParams::meta(ParamSlot::camTouch).flags & kMod) == 0,
          "a modulation target is flagged, a switch is not");
    check(std::string(SceneParams::meta(ParamSlot::paletteAmount0).label) == "Palette 1 amount" &&
              std::string(SceneParams::meta(ParamSlot::paletteAmount7).label) == "Palette 8 amount" &&
              std::string(SceneParams::meta(ParamSlot::paletteShift).label) == "Palette position",
          "each palette amount and the position is a control of its own");
    for (int slot = static_cast<int>(ParamSlot::paletteAmount0); slot <= static_cast<int>(ParamSlot::paletteShift); ++slot) {
        const ParamMeta& m = SceneParams::meta(static_cast<ParamSlot>(slot));
        // Palette 2's amount is what PaletteMix writes: on shader styles until every style kind draws the stack.
        const auto scope = slot == static_cast<int>(ParamSlot::paletteAmount1) ? catalogue::Scope::ShaderLook : catalogue::Scope::Universal;
        check(m.scope == scope && m.rollLo == 0.0f && m.rollHi == 0.0f && (m.flags & kMod) != 0,
              std::string(SceneParams::spec(static_cast<ParamSlot>(slot)).name) + ": a target, never dealt by the param roll");
    }
    check(!SceneParams::meta(ParamSlot::paletteMix).live && !SceneParams::meta(ParamSlot::palette2).live &&
              !SceneParams::meta(ParamSlot::paletteLut).live,
          "Palette blend, Palette 2 and Colour map are retired: the stack draws more than one palette and the colour maps");
    {
        const ParamMeta& m = SceneParams::meta(ParamSlot::layerMix);
        const ParamSpec& s = SceneParams::spec(ParamSlot::layerMix);
        check(std::string(m.label) == "Layer mix" && m.scope == catalogue::Scope::Universal && (m.flags & kMod) != 0 &&
                  m.rollLo == 0.0f && m.rollHi == 0.0f && s.defaultValue == 0.0f && s.min == -1.0f && s.max == 1.0f,
              "the layer's mix is an offset a target writes on every style, 0 in every look, never rolled");
    }
    check(!SceneParams::meta(ParamSlot::pulse).live && !SceneParams::meta(ParamSlot::glitch).live, "retired rows have no meta");
    const ParamMeta& kaleido = SceneParams::meta(ParamSlot::kaleidoFolds);
    check(std::string(kaleido.label) == "Kaleidoscope" && kaleido.scope == catalogue::Scope::Universal && (kaleido.flags & kMod) != 0 &&
              !SceneParams::meta(ParamSlot::symmetry).live && !SceneParams::meta(ParamSlot::kaleidoscope).live,
          "Kaleidoscope is kaleidoFolds alone, a modulation target on every style");
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << SceneParams::schemaText();
        return 0;
    }
    theTableIsTheWire();
    everyRowIsWellFormed();
    getAndSetRoundTripByNameAndSlot();
    anOldLookWithRetiredNamesStillLoads();
    theWireRoundTrips();
    aBrokenWireKeepsTheDefaults();
    fadeGlidesAndHoldSnaps();
    theLfoClampsAreTheTablesRanges();
    theCataloguesRangesAgree();
    everyPickerLabelsItsRange();
    theKaleidoscopeIsOneSlider();
    theTextIsTheTable();
    palettesAreTheShellsOldTable();
    everyLiveRowHasOneMeta();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/params_schema.txt");
    if (failures == 0) std::printf("params_schema_test: all checks passed\n");
    return failures;
}
