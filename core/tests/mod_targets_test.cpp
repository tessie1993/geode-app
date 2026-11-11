// Host-side checks for the modulation-target table (core/viz/ModTargets, #642): which style each LFO,
// envelope and reaction target reaches, which slot a chained target steers, the config defaults and
// ranges, the polarity a source starts on, and the text table the shell reads instead of keeping its own.
// Built and run by CI (.github/workflows/android.yml, "Modulation target host tests"); no GL.
//
//   mod_targets_test core/tests/mod_targets.txt           checks, including text() against the fixture
//   mod_targets_test --write core/tests/mod_targets.txt   rewrites the fixture after a deliberate change
//
// The fixture is what the shell's JVM tests read in place of geode_mod_targets_text (the geode.modTargets
// property, dev.geode.render.ModTable). The expected scopes below are the Kotlin LfoTarget table this
// replaced, so the port changed no target's reach.
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

// The old Kotlin LfoTarget rows (name, ParamScope), in wire order.
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
    {"FLOW_STRENGTH", "UNIVERSAL"}, {"WATER_RIPPLE", "WATER"}, {"RIPPLE_OVERLAY", "RIPPLE_OVERLAY"},
    {"LFO1_RATE", "UNIVERSAL"}, {"LFO1_DEPTH", "UNIVERSAL"}, {"LFO2_RATE", "UNIVERSAL"}, {"LFO2_DEPTH", "UNIVERSAL"},
    {"LFO3_RATE", "UNIVERSAL"}, {"LFO3_DEPTH", "UNIVERSAL"},
    {"CAM_FLY", "UNIVERSAL"}, {"CAM_ORBIT", "UNIVERSAL"}, {"CAM_PUSH", "UNIVERSAL"}, {"CAM_YAW", "UNIVERSAL"},
    {"CAM_PITCH", "UNIVERSAL"}, {"CAM_ROLL", "UNIVERSAL"}, {"CAM_FOV", "UNIVERSAL"}, {"CAM_SHAKE", "UNIVERSAL"},
    {"FLOW_CURL", "UNIVERSAL"}, {"FLOW_FORCE", "UNIVERSAL"}, {"DIVE_SPEED", "UNIVERSAL"},
};

void everyTargetKeepsItsNameAndScope() {
    check(modtargets::kTargetCount == static_cast<int>(kOldTargets.size()), "one row per LfoTarget");
    for (int i = 0; i < modtargets::kTargetCount && i < static_cast<int>(kOldTargets.size()); ++i) {
        const auto t = static_cast<LfoTarget>(i);
        check(std::string(modtargets::name(t)) == kOldTargets[i].first, "name of target " + std::to_string(i));
        check(std::string(catalogue::name(modtargets::scopeOf(t))) == kOldTargets[i].second,
              std::string("scope of ") + kOldTargets[i].first);
    }
    check(static_cast<int>(LfoTarget::CamShake) == 52 && static_cast<int>(LfoTarget::EndlessZoomSpeed) == 55, "wire ordinals");
}

void onlyTheRetiredSlotsAreHidden() {
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        check(modtargets::offered(t) == (t != LfoTarget::Retired5 && t != LfoTarget::Retired25), std::string("offered ") + modtargets::name(t));
    }
}

void reachFollowsTheScope() {
    check(modtargets::reaches(LfoTarget::Morph, "julia"), "Morph reaches a shader style");
    check(!modtargets::reaches(LfoTarget::Morph, "silk_web"), "Morph does not reach Silk");
    check(!modtargets::reaches(LfoTarget::Speed, "milkdrop"), "Speed does not reach MilkDrop");
    check(modtargets::reaches(LfoTarget::FluidCurl, "fluid"), "Fluid curl reaches Fluid");
    check(!modtargets::reaches(LfoTarget::FluidCurl, "julia"), "Fluid curl does not reach a shader style");
    check(modtargets::reaches(LfoTarget::Lfo2Depth, "milkdrop"), "a chained target reaches every style");
    check(!modtargets::reaches(LfoTarget::Retired5, "julia") && !modtargets::reaches(LfoTarget::Retired25, "julia"),
          "a retired slot reaches nothing");
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
}

void offerMaskHasABitPerTarget() {
    for (const auto& id : catalogue::allIds()) {
        for (int picker = modtargets::kPickerReaction; picker < LfoEngine::kSlots; ++picker) {
            std::uint64_t want = 0;
            for (int i = 0; i < modtargets::kTargetCount; ++i) {
                if (modtargets::offeredTo(static_cast<LfoTarget>(i), id, picker)) want |= std::uint64_t{1} << i;
            }
            check(modtargets::offerMask(id, picker) == want, "offer mask of " + id + " for picker " + std::to_string(picker));
        }
        check(modtargets::offerMask(id, modtargets::kPickerEnvelope) ==
                  [&] {
                      std::uint64_t m = 0;
                      for (int i = 0; i < modtargets::kTargetCount; ++i)
                          if (modtargets::reaches(static_cast<LfoTarget>(i), id)) m |= std::uint64_t{1} << i;
                      return m;
                  }(),
              "an envelope is offered exactly what reaches " + id);
    }
}

bool hasLine(const std::string& text, const std::string& line) { return text.find("\n" + line + "\n") != std::string::npos; }

void textCarriesEveryRow() {
    const std::string& t = modtargets::text();
    check(t.rfind("modulation|2\n", 0) == 0, "header first");
    check(hasLine(t, "config|2|0.05|60|0.3|3"), "config row: default rate, rate range, default depth, slots");
    check(hasLine(t, "target|10|MORPH|SHADER_LOOK|-1|1"), "a scene target row");
    check(hasLine(t, "target|5|RETIRED_5|UNIVERSAL|-1|0") && hasLine(t, "target|25|RETIRED_25|UNIVERSAL|-1|0"),
          "the retired rows are not offered");
    check(hasLine(t, "target|43|LFO3_RATE|UNIVERSAL|2|1"), "a chained row names its slot");
    check(hasLine(t, "source|0|LFO|BIPOLAR") && hasLine(t, "source|1|BASS|POSITIVE") && hasLine(t, "source|8|STEREO_PAN|BIPOLAR"),
          "source rows carry their natural polarity");
    check(hasLine(t, "curve|3|SMOOTH") && hasLine(t, "wave|4|RANDOM") && hasLine(t, "polarity|2|NEGATIVE"), "enum rows");
    // One reach row per style (and "*" for an id the catalogue does not know): the offer mask, in hex, for a
    // reaction, an envelope and each wave slot, so the shell reads the answer and never works it out.
    const auto row = [](const std::string& id) {
        char buf[32];
        std::string out = "reach|" + id;
        for (int picker = modtargets::kPickerReaction; picker < LfoEngine::kSlots; ++picker) {
            std::snprintf(buf, sizeof buf, "|%llx", static_cast<unsigned long long>(modtargets::offerMask(id, picker)));
            out += buf;
        }
        return out;
    };
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
