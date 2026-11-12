// Host-side checks for the modulation-target table (core/viz/ModTargets, #642): which style each LFO,
// envelope and reaction target reaches, which slot a chained target steers, the config defaults and
// ranges, the polarity a source starts on, and the text table the shell reads instead of keeping its own.
// Built and run by CI (.github/workflows/android.yml, "Modulation target host tests"); no GL.
//
//   mod_targets_test core/tests/mod_targets.txt           checks, including text() against the fixture
//   mod_targets_test --write core/tests/mod_targets.txt   rewrites the fixture after a deliberate change
//
// The fixture pins the table's text. The expected scopes below are each target's reach as first shipped,
// so no target's reach moves unseen; since the scopes are read from the params schema's meta rows
// (#1005 PR 4a), the three Liquid and dive targets carry their Customize control's scope.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ReactionDna.hpp"

using namespace geode::viz;
using catalogue::Scope;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// Every target as first shipped (name, scope), in wire order.
const std::vector<std::pair<const char*, const char*>> kOldTargets = {
    {"NONE", "UNIVERSAL"}, {"SPEED", "SCENE_CLOCK"}, {"ZOOM", "UNIVERSAL"}, {"ROTATION", "UNIVERSAL"},
    {"SWAY", "UNIVERSAL"}, {"RETIRED_5", "UNIVERSAL"}, {"DRIFT_X", "UNIVERSAL"}, {"DRIFT_Y", "UNIVERSAL"},
    {"WARP", "UNIVERSAL"}, {"RIPPLE", "UNIVERSAL"}, {"MORPH", "SHADER_LOOK"}, {"TWIST", "UNIVERSAL"},
    {"TILE", "UNIVERSAL"}, {"PIXELATE", "UNIVERSAL"}, {"POSTERIZE", "UNIVERSAL"}, {"COLOR_SHIFT", "UNIVERSAL"},
    {"PALETTE_MIX", "SHADER_LOOK"}, {"SATURATION", "UNIVERSAL"}, {"BRIGHTNESS", "UNIVERSAL"}, {"INTENSITY", "UNIVERSAL"},
    {"BLOOM", "UNIVERSAL"}, {"TEMPERATURE", "UNIVERSAL"}, {"TURBULENCE", "TURBULENCE"}, {"CHROMA_AB", "UNIVERSAL"},
    {"VIGNETTE", "UNIVERSAL"}, {"RETIRED_25", "UNIVERSAL"}, {"FISHEYE", "UNIVERSAL"}, {"PARTICLE_SIZE", "PARTICLE_SPRITE"},
    {"TRAIL_LENGTH", "TRAIL_LENGTH"}, {"FLUID_CURL", "FLUID_SIM"}, {"FLUID_RADIUS", "EMITTERS"}, {"FLUID_FORCE", "EMITTERS"},
    {"FLUID_GLOW", "FLUID_SIM"}, {"FLUID_FADE", "FLUID_SIM"}, {"FLUID_CATCH_PULL", "JOURNEY"}, {"FLUID_CATCH_RADIUS", "JOURNEY"},
    {"FLOW_STRENGTH", "UNIVERSAL"}, {"RETIRED_37", "UNIVERSAL"}, {"RETIRED_38", "UNIVERSAL"},
    {"LFO1_RATE", "UNIVERSAL"}, {"LFO1_DEPTH", "UNIVERSAL"}, {"LFO2_RATE", "UNIVERSAL"}, {"LFO2_DEPTH", "UNIVERSAL"},
    {"LFO3_RATE", "UNIVERSAL"}, {"LFO3_DEPTH", "UNIVERSAL"},
    {"CAM_FLY", "UNIVERSAL"}, {"CAM_ORBIT", "UNIVERSAL"}, {"CAM_PUSH", "UNIVERSAL"}, {"CAM_YAW", "UNIVERSAL"},
    {"CAM_PITCH", "UNIVERSAL"}, {"CAM_ROLL", "UNIVERSAL"}, {"CAM_FOV", "UNIVERSAL"}, {"CAM_SHAKE", "UNIVERSAL"},
    // Were UNIVERSAL while this table kept its own scopes; now each is its Customize control's (Flow curl,
    // Flow force, Dive speed), so a wave is offered exactly where the slider is shown.
    {"FLOW_CURL", "FLOW_FIELD_SIM"}, {"FLOW_FORCE", "FLOW_FIELD_SIM"}, {"DIVE_SPEED", "ENDLESS_ZOOM"},
    // Appended after the port: the dive's spiral and aim, on Dive speed's scope.
    {"DIVE_TWIST", "ENDLESS_ZOOM"}, {"DIVE_AIM_X", "ENDLESS_ZOOM"}, {"DIVE_AIM_Y", "ENDLESS_ZOOM"},
    // A macro knob (#owner 2026-11-12): a wave or an envelope turns the knob, which moves what it links.
    {"MACRO", "UNIVERSAL"},
    // The palette stack's amounts and position (viz/Palette.hpp), on their Look controls' scope.
    {"PALETTE_AMOUNT_1", "UNIVERSAL"}, {"PALETTE_AMOUNT_3", "UNIVERSAL"}, {"PALETTE_AMOUNT_4", "UNIVERSAL"},
    {"PALETTE_AMOUNT_5", "UNIVERSAL"}, {"PALETTE_AMOUNT_6", "UNIVERSAL"}, {"PALETTE_AMOUNT_7", "UNIVERSAL"},
    {"PALETTE_AMOUNT_8", "UNIVERSAL"}, {"PALETTE_SHIFT", "UNIVERSAL"},
    // The kaleidoscope's folds, the slider that replaced its switch: on Kaleidoscope's scope.
    {"KALEIDO_FOLDS", "UNIVERSAL"},
    // Grow and layers: on the family libraries' scope, the styles whose library draws them.
    {"SPAWN", "FAMILY_FORM"}, {"FRACTAL", "FAMILY_FORM"}, {"FRONT_LAYER", "FAMILY_FORM"}, {"BACK_LAYER", "FAMILY_FORM"},
};

void everyTargetKeepsItsNameAndScope() {
    check(modtargets::kTargetCount == static_cast<int>(kOldTargets.size()), "one row per LfoTarget");
    for (int i = 0; i < modtargets::kTargetCount && i < static_cast<int>(kOldTargets.size()); ++i) {
        const auto t = static_cast<LfoTarget>(i);
        check(std::string(modtargets::name(t)) == kOldTargets[i].first, "name of target " + std::to_string(i));
        check(std::string(catalogue::name(modtargets::scopeOf(t))) == kOldTargets[i].second,
              std::string("scope of ") + kOldTargets[i].first);
    }
    check(static_cast<int>(LfoTarget::CamShake) == 52 && static_cast<int>(LfoTarget::EndlessZoomSpeed) == 55 &&
              static_cast<int>(LfoTarget::DiveAimY) == 58 && static_cast<int>(LfoTarget::Macro) == 59 &&
              static_cast<int>(LfoTarget::PaletteAmount1) == 60 && static_cast<int>(LfoTarget::PaletteShift) == 67 &&
              static_cast<int>(LfoTarget::KaleidoFolds) == 68 && static_cast<int>(LfoTarget::Spawn) == 69 &&
              static_cast<int>(LfoTarget::BackLayer) == 72 && kLastLfoTarget == LfoTarget::BackLayer,
          "wire ordinals");
    check(paramSlotOf(LfoTarget::Spawn) == static_cast<int>(ParamSlot::spawn) &&
              paramSlotOf(LfoTarget::Fractal) == static_cast<int>(ParamSlot::fractal) &&
              paramSlotOf(LfoTarget::FrontLayer) == static_cast<int>(ParamSlot::frontLayer) &&
              paramSlotOf(LfoTarget::BackLayer) == static_cast<int>(ParamSlot::backLayer),
          "each Grow target writes its own row");
    for (LfoTarget t : {LfoTarget::Spawn, LfoTarget::Fractal, LfoTarget::FrontLayer, LfoTarget::BackLayer}) {
        check(!isLuminanceTarget(t) && modtargets::reaches(t, "petal_rose") && !modtargets::reaches(t, "plasma") &&
                  !modtargets::reaches(t, "flame_veil"),
              std::string(modtargets::name(t)) + ": a shape, offered on a family style that draws it and nowhere else");
    }
    check(paramSlotOf(LfoTarget::KaleidoFolds) == static_cast<int>(ParamSlot::kaleidoFolds) && !isLuminanceTarget(LfoTarget::KaleidoFolds),
          "the kaleidoscope's target writes its folds, a shape and no luminance target");
    check(paramSlotOf(LfoTarget::PaletteAmount1) == static_cast<int>(ParamSlot::paletteAmount0) &&
              paramSlotOf(LfoTarget::PaletteAmount3) == static_cast<int>(ParamSlot::paletteAmount2) &&
              paramSlotOf(LfoTarget::PaletteAmount8) == static_cast<int>(ParamSlot::paletteAmount7) &&
              paramSlotOf(LfoTarget::PaletteShift) == static_cast<int>(ParamSlot::paletteShift),
          "each palette target writes its own row");
    check(paramSlotOf(LfoTarget::PaletteMix) == static_cast<int>(ParamSlot::paletteAmount1), "Palette blend's target is Palette 2's amount");
    for (LfoTarget t : {LfoTarget::PaletteAmount1, LfoTarget::PaletteAmount3, LfoTarget::PaletteAmount8, LfoTarget::PaletteShift}) {
        check(isLuminanceTarget(t), std::string(modtargets::name(t)) + " is held to the flash rate and the route floors");
    }
}

bool retired(LfoTarget t) {
    return t == LfoTarget::Retired5 || t == LfoTarget::Retired25 || t == LfoTarget::Retired37 || t == LfoTarget::Retired38;
}

void onlyTheRetiredSlotsAreHidden() {
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        check(modtargets::offered(t) == !retired(t), std::string("offered ") + modtargets::name(t));
    }
}

// A target's scope and whether it is offered are its field's meta row (viz/ParamTable.inc): kMod is set on
// exactly the rows a target writes, a target's scope is its Customize control's, and a retired field's
// target is offered nowhere.
void targetsFollowTheirMetaRow() {
    std::vector<int> writers(SceneParams::kFieldCount, 0);
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        const int slot = paramSlotOf(t);
        if (slot < 0) continue;
        ++writers[static_cast<size_t>(slot)];
        const ParamMeta& m = SceneParams::meta(static_cast<ParamSlot>(slot));
        check(m.live, std::string(modtargets::name(t)) + " writes a live field");
        check(modtargets::scopeOf(t) == catalogue::scopeOf(m.label), std::string(modtargets::name(t)) + " has its control's scope");
    }
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        const ParamMeta& m = SceneParams::meta(static_cast<ParamSlot>(slot));
        const std::string field = SceneParams::fieldNames()[static_cast<size_t>(slot)];
        check(((m.flags & kMod) != 0) == (writers[static_cast<size_t>(slot)] > 0), field + ": kMod iff a target writes it");
        check(writers[static_cast<size_t>(slot)] <= 1, field + ": one target per field");
    }
}

void reachFollowsTheScope() {
    check(modtargets::reaches(LfoTarget::Morph, "julia"), "Morph reaches a shader style");
    check(!modtargets::reaches(LfoTarget::Morph, "silk_web"), "Morph does not reach Silk");
    check(!modtargets::reaches(LfoTarget::Speed, "milkdrop"), "Speed does not reach MilkDrop");
    check(modtargets::reaches(LfoTarget::FluidCurl, "fluid"), "Fluid curl reaches Fluid");
    check(!modtargets::reaches(LfoTarget::FluidCurl, "julia"), "Fluid curl does not reach a shader style");
    check(modtargets::reaches(LfoTarget::Lfo2Depth, "milkdrop"), "a chained target reaches every style");
    for (LfoTarget t : {LfoTarget::Retired5, LfoTarget::Retired25, LfoTarget::Retired37, LfoTarget::Retired38}) {
        check(!modtargets::reaches(t, "julia"), "a retired slot reaches nothing");
    }
    check(static_cast<int>(LfoTarget::Retired37) == 37 && static_cast<int>(LfoTarget::Retired38) == 38, "the water slots keep their ordinals");
    check(!modtargets::reaches(static_cast<LfoTarget>(99), "julia"), "an unknown target reaches nothing");
}

void musicActionsReachWhatTheirTargetsReach() {
    for (const auto& id : catalogue::allIds()) {
        std::uint32_t want = 0;
        for (int a = 0; a < kMusicActionCount; ++a) {
            if (modtargets::reaches(musicActionTarget(static_cast<MusicAction>(a)), id)) want |= 1u << a;
        }
        check(modtargets::musicActionsFor(id) == want, "music actions of " + id);
    }
    // Every action's target is a universal one today, so every style offers all seven.
    check(modtargets::musicActionsFor("milkdrop") == (1u << kMusicActionCount) - 1u, "MilkDrop offers every action");
}

void chainedTargetsSteerTheirSlot() {
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        ModChain c{};
        check(modtargets::chainSlot(t) == (chainOf(t, c) ? c.slot : -1), std::string("chain slot of ") + modtargets::name(t));
    }
    check(modtargets::chainSlot(LfoTarget::Lfo3Rate) == 2, "Wave 3 speed steers slot 2");
}

void followersStartUpOnly() {
    check(modtargets::naturalPolarity(ModSource::Lfo) == ModPolarity::Bipolar, "an oscillator swings both ways");
    check(modtargets::naturalPolarity(ModSource::StereoPan) == ModPolarity::Bipolar, "pan swings both ways");
    for (ModSource s : {ModSource::Bass, ModSource::Mid, ModSource::Treble, ModSource::Level, ModSource::Brightness,
                        ModSource::Transient, ModSource::StereoWidth}) {
        check(modtargets::naturalPolarity(s) == ModPolarity::Positive, "a follower starts up only");
    }
}

// Who picks a target decides what it may move (#642 review): a reaction route moves scene params only
// (ReactionEngine skips chained routes); an envelope may steer any wave; a wave only a LATER wave, the
// order LfoEngine::step reads them in. Scene targets follow the style's reach for every picker.
void pickersOfferWhatTheEngineRuns() {
    using modtargets::kPickerEnvelope;
    using modtargets::kPickerReaction;
    check(!modtargets::offeredTo(LfoTarget::Lfo1Rate, "julia", kPickerReaction), "a reaction steers no wave");
    check(modtargets::offeredTo(LfoTarget::Lfo1Rate, "julia", kPickerEnvelope), "an envelope steers wave 1");
    check(modtargets::offeredTo(LfoTarget::Lfo3Depth, "milkdrop", kPickerEnvelope), "an envelope steers wave 3 anywhere");
    check(!modtargets::offeredTo(LfoTarget::Lfo1Rate, "julia", 0), "wave 1 does not steer itself");
    check(modtargets::offeredTo(LfoTarget::Lfo2Rate, "julia", 0) && modtargets::offeredTo(LfoTarget::Lfo3Depth, "julia", 0),
          "wave 1 steers the later waves");
    check(!modtargets::offeredTo(LfoTarget::Lfo1Depth, "julia", 1), "wave 2 does not steer an earlier wave");
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        if (modtargets::chainSlot(t) >= 0) check(!modtargets::offeredTo(t, "julia", 2), "the last wave steers no wave");
    }
    for (int picker : {kPickerReaction, kPickerEnvelope, 0, 1, 2}) {
        check(modtargets::offeredTo(LfoTarget::Morph, "julia", picker) && !modtargets::offeredTo(LfoTarget::Morph, "silk_web", picker),
              "a scene target follows the style's reach for picker " + std::to_string(picker));
        check(!modtargets::offeredTo(LfoTarget::Retired5, "julia", picker) && !modtargets::offeredTo(LfoTarget::Retired25, "julia", picker),
              "a retired slot is offered to no picker");
    }
    check(!modtargets::offeredTo(LfoTarget::Zoom, "julia", 3) && !modtargets::offeredTo(LfoTarget::Zoom, "julia", -3),
          "an unknown picker is offered nothing");
    // A knob is the waves' and the envelopes' on every style, never a reaction route's, and writes no param.
    for (const auto& id : catalogue::allIds()) {
        check(!modtargets::offeredTo(LfoTarget::Macro, id, kPickerReaction), "a reaction route turns no knob on " + id);
        for (int picker : {kPickerEnvelope, 0, 1, 2}) check(modtargets::offeredTo(LfoTarget::Macro, id, picker), "a knob is offered on " + id);
    }
    check(paramSlotOf(LfoTarget::Macro) < 0 && modtargets::chainSlot(LfoTarget::Macro) < 0, "a knob is no param and steers no wave");
}

void offerMaskHasABitPerTarget() {
    for (const auto& id : catalogue::allIds()) {
        for (int picker = modtargets::kPickerReaction; picker < LfoEngine::kSlots; ++picker) {
            modtargets::Mask want;
            for (int i = 0; i < modtargets::kTargetCount; ++i) want[static_cast<size_t>(i)] = modtargets::offeredTo(static_cast<LfoTarget>(i), id, picker);
            check(modtargets::offerMask(id, picker) == want, "offer mask of " + id + " for picker " + std::to_string(picker));
        }
        check(modtargets::offerMask(id, modtargets::kPickerEnvelope) ==
                  [&] {
                      modtargets::Mask m;
                      for (int i = 0; i < modtargets::kTargetCount; ++i) m[static_cast<size_t>(i)] = modtargets::reaches(static_cast<LfoTarget>(i), id);
                      return m;
                  }(),
              "an envelope is offered exactly what reaches " + id);
    }
    // More targets than 64 bits: the hex the shell reads carries every one.
    modtargets::Mask m;
    check(modtargets::hex(m) == "0", "an empty mask is 0");
    m.set(0).set(4);
    check(modtargets::hex(m) == "11", "low bits, no leading zeros");
    m.reset().set(static_cast<size_t>(LfoTarget::PaletteShift));
    check(modtargets::hex(m) == "8" + std::string(16, '0'), "ordinal 67 is the top bit of the seventeenth digit");
    m.reset().set(static_cast<size_t>(LfoTarget::KaleidoFolds));
    check(modtargets::hex(m) == "1" + std::string(17, '0'), "ordinal 68 opens an eighteenth digit");
    m.reset().set(static_cast<size_t>(LfoTarget::BackLayer));
    check(modtargets::hex(m) == "1" + std::string(18, '0'), "ordinal 72 opens a nineteenth digit");
}

bool hasLine(const std::string& text, const std::string& line) { return text.find("\n" + line + "\n") != std::string::npos; }

void textCarriesEveryRow() {
    const std::string& t = modtargets::text();
    check(t.rfind("modulation|3\n", 0) == 0, "header first");
    check(hasLine(t, "config|2|0.05|60|0.3|3"), "config row: default rate, rate range, default depth, slots");
    check(hasLine(t, "target|10|MORPH|SHADER_LOOK|-1|1"), "a scene target row");
    check(hasLine(t, "target|5|RETIRED_5|UNIVERSAL|-1|0") && hasLine(t, "target|25|RETIRED_25|UNIVERSAL|-1|0"),
          "the retired rows are not offered");
    check(hasLine(t, "target|37|RETIRED_37|UNIVERSAL|-1|0") && hasLine(t, "target|38|RETIRED_38|UNIVERSAL|-1|0"),
          "nor are the Water style's and the ripple overlay's old slots");
    check(hasLine(t, "target|43|LFO3_RATE|UNIVERSAL|2|1"), "a chained row names its slot");
    check(hasLine(t, "source|0|LFO|BIPOLAR") && hasLine(t, "source|1|BASS|POSITIVE") && hasLine(t, "source|8|STEREO_PAN|BIPOLAR"),
          "source rows carry their natural polarity");
    check(hasLine(t, "curve|3|SMOOTH") && hasLine(t, "wave|4|RANDOM") && hasLine(t, "polarity|2|NEGATIVE"), "enum rows");
    // One reach row per style (and "*" for an id the catalogue does not know): the offer mask, in hex, for a
    // reaction, an envelope and each wave slot, so the shell reads the answer and never works it out.
    const auto row = [](const std::string& id) {
        std::string out = "reach|" + id;
        for (int picker = modtargets::kPickerReaction; picker < LfoEngine::kSlots; ++picker) out += "|" + modtargets::hex(modtargets::offerMask(id, picker));
        return out;
    };
    check(hasLine(t, "target|72|BACK_LAYER|FAMILY_FORM|-1|1"), "the last target row");
    for (const auto& id : catalogue::allIds()) check(hasLine(t, row(id)), "reach row of " + id);
    check(hasLine(t, "reach|*" + row("").substr(6)), "the fallback reach row answers as an unknown id");
    check(t.back() == '\n', "ends with a newline");
}

void textEqualsTheFixture(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    check(in.good() || in.eof(), std::string("read ") + path);
    check(ss.str() == modtargets::text(),
          std::string("text() differs from ") + path + "; after a deliberate change run mod_targets_test --write " + path);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << modtargets::text();
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    everyTargetKeepsItsNameAndScope();
    onlyTheRetiredSlotsAreHidden();
    targetsFollowTheirMetaRow();
    reachFollowsTheScope();
    musicActionsReachWhatTheirTargetsReach();
    chainedTargetsSteerTheirSlot();
    followersStartUpOnly();
    pickersOfferWhatTheEngineRuns();
    offerMaskHasABitPerTarget();
    textCarriesEveryRow();
    textEqualsTheFixture(argc > 1 ? argv[1] : "core/tests/mod_targets.txt");
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("mod_targets_test: OK\n");
    return 0;
}
