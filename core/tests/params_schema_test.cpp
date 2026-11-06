// Host-side checks for the scene params schema (core/viz/ParamTable.inc, expanded by core/viz/Params and
// core/viz/ParamsFields): one row per field gives its name, kind, default, range and wire slot, and the
// struct, the wire, get/set, lerpParams, the LFO clamps and the shell's text table all come from it.
// Built and run by CI (.github/workflows/android.yml, "Native host tests") with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"
#include "viz/Params.hpp"
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
constexpr int kWireFloats = 163;

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
    check(static_cast<int>(ParamSlot::liquidSurface) == kWireFloats - 1, "liquidSurface is the wire's last float");
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

void theWireRoundTrips() {
    SceneParams p;
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        p.setSlot(slot, probes(SceneParams::schema()[static_cast<size_t>(slot)]).back());
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
    {LfoTarget::Pulse, "pulse", 0.0f, 1.0f},            {LfoTarget::DriftX, "driftX", -1.0f, 1.0f},
    {LfoTarget::DriftY, "driftY", -1.0f, 1.0f},         {LfoTarget::Warp, "warp", 0.0f, 1.0f},
    {LfoTarget::Ripple, "ripple", 0.0f, 1.0f},          {LfoTarget::Morph, "morph", 0.0f, 1.0f},
    {LfoTarget::Twist, "twist", -1.0f, 1.0f},           {LfoTarget::Tile, "tile", 1.0f, 6.0f},
    {LfoTarget::Pixelate, "pixelate", 0.0f, 1.0f},      {LfoTarget::Posterize, "posterize", 0.0f, 1.0f},
    {LfoTarget::PaletteMix, "paletteMix", 0.0f, 1.0f},  {LfoTarget::Saturation, "saturation", 0.0f, 1.5f},
    {LfoTarget::Brightness, "brightness", 0.2f, 2.0f},  {LfoTarget::Intensity, "intensity", 0.2f, 2.0f},
    {LfoTarget::Bloom, "bloom", 0.0f, 1.0f},            {LfoTarget::Temperature, "temperature", -1.0f, 1.0f},
    {LfoTarget::Turbulence, "turbulence", 0.0f, 1.5f},  {LfoTarget::ChromaAb, "chromaAb", 0.0f, 1.0f},
    {LfoTarget::Vignette, "vignette", 0.0f, 1.0f},      {LfoTarget::Fisheye, "fisheye", -1.0f, 1.0f},
    {LfoTarget::ParticleSize, "particleSize", 0.3f, 2.5f}, {LfoTarget::TrailLength, "trailLength", 0.05f, 0.98f},
    {LfoTarget::FluidCurl, "fluidCurl", 0.0f, 50.0f},   {LfoTarget::FluidRadius, "fluidSplatRadius", 0.02f, 0.4f},
    {LfoTarget::FluidForce, "fluidSplatForce", 0.0f, 3.0f}, {LfoTarget::FluidGlow, "fluidBloomIntensity", 0.1f, 2.0f},
    {LfoTarget::FluidFade, "fluidDensityDissipation", 0.0f, 4.0f}, {LfoTarget::FluidCatchPull, "fluidCatchPull", 0.0f, 3.0f},
    {LfoTarget::FluidCatchRadius, "fluidCatchRadius", 0.03f, 0.3f}, {LfoTarget::FlowStrength, "flowStrength", 0.0f, 1.0f},
    {LfoTarget::WaterRipple, "waterRippleStrength", 0.0f, 2.0f}, {LfoTarget::RippleOverlay, "rippleOverlayStrength", 0.0f, 1.0f},
    {LfoTarget::CamFly, "camFly", 0.0f, 2.5f},          {LfoTarget::CamOrbit, "camOrbit", 0.0f, 1.0f},
    {LfoTarget::CamPush, "camPush", -1.0f, 1.0f},       {LfoTarget::CamYaw, "camYaw", -1.0f, 1.0f},
    {LfoTarget::CamPitch, "camPitch", -1.0f, 1.0f},     {LfoTarget::CamRoll, "camRoll", -1.0f, 1.0f},
    {LfoTarget::CamFov, "camFov", 0.5f, 2.0f},          {LfoTarget::CamShake, "camShake", 0.0f, 1.0f},
    {LfoTarget::FlowCurl, "flowCurl", 0.0f, 50.0f},     {LfoTarget::FlowForce, "flowForce", 0.0f, 3.0f},
    {LfoTarget::EndlessZoomSpeed, "endlessZoomSpeed", 0.0f, 1.2f},
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
    for (LfoTarget t : {LfoTarget::None, LfoTarget::Retired25, LfoTarget::Lfo1Rate, LfoTarget::Lfo3Depth}) {
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
    while (std::getline(lines, line)) {
        const auto f = split(line);
        if (slot >= SceneParams::kFieldCount) {
            check(false, "a row past the wire: " + line);
            break;
        }
        const ParamSpec& s = SceneParams::schema()[static_cast<size_t>(slot)];
        if (f.size() != 8) {
            check(false, "eight fields: " + line);
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
        ++slot;
    }
    check(slot == kWireFloats, "one row per slot");
    // The largest seed is a whole number on the wire, not "1.67772e+07".
    check(SceneParams::schemaText().find("|reactSeed|INT|0|0|16777215|HOLD\n") != std::string::npos, "reactSeed's max is exact");
    check(SceneParams::schemaText().find("|waterDamping|FLOAT|0.985|0.9|0.999|FADE\n") != std::string::npos, "short numbers stay short");
}

}  // namespace

int main() {
    theTableIsTheWire();
    everyRowIsWellFormed();
    getAndSetRoundTripByNameAndSlot();
    theWireRoundTrips();
    fadeGlidesAndHoldSnaps();
    theLfoClampsAreTheTablesRanges();
    theCataloguesRangesAgree();
    theTextIsTheTable();
    if (failures == 0) std::printf("params_schema_test: all checks passed\n");
    return failures;
}
