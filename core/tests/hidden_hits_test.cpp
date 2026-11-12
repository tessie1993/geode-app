// No hidden hit reactions (owner, 2026-11-12): what moves on a hit is the user's reaction matrix
// (core/viz/Reactions) and the controls the user sets, never a scene, a pass or a simulation reacting to
// the hit on its own. resolver_test holds the params to it ("with no route and the Motion dials at 0 no
// param hears the sound"); a scene, though, takes the raw sound frame and draws on the GL thread, where
// no host test can watch it. This reads the sources instead, the nearest a host test gets.
// Built and run by CI (.github/workflows/native-host-tests.yml):
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror core/tests/hidden_hits_test.cpp -o hidden_hits_test
//   ./hidden_hits_test core/
//
// A hit read is live::hit() or live::Edge (the onset envelope and the onset trigger the scenes were
// tuned on), or a member read of the frame's onset, transient and drum fields (onsetEnv, transient,
// transientEnv, kick, kickEnv, kick16, kickToggle, ...). Comments do not count. Three rules:
//
// 1. Nothing in core/ reads the retired beatResponse field (SceneParams::retired). It only ever scaled
//    the hidden hit reactions: MilkDrop's zoom kick and the simulations' beat pulses, glitches, drum
//    forces and droplets, all removed with it.
// 2. Under core/viz, a hit is read only where the user decides it moves something: kAllowed, each with
//    the control that says so.
// 3. kGaps are the scenes that still read the drums on their own: counted kicks, kick toggles and
//    randoms, drum envelopes blended into a level. They are known, listed in the change that retired
//    beatResponse, and wait for the owner's decision. The list may only shrink: a file that no longer
//    reads a hit fails here until it leaves the list, and a new file fails at once.
//
// What this does not see: the shader styles read the raw frame through lib_sound (SND_KICK_ENV,
// SND_ONSET_ENV, ...) and are outside these rules for now.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        failures++;
    }
}

// Where a hit may be read, under core/viz, and why the user is the one who moves it.
const std::map<std::string, const char*> kAllowed = {
    {"LiveSignal.hpp", "defines live::hit and live::Edge"},
    {"Reactions.cpp", "the reaction matrix itself: the user's routes and the style DNA under the hand mask"},
    {"Lfo.cpp", "an LFO the user set on the Transient source (the transient envelope)"},
    {"Adsr.cpp", "an envelope the user set, triggered on the onset"},
    {"NoteResponse.cpp", "Note & key reactions, the user's switch (Settings > Behaviour) and drum amount"},
    {"MotionField.cpp", "the Motion dials the user sets; at 0 nothing hears the sound (resolver_test)"},
    {"CameraRig.cpp", "the camera's music moves (Shake, Reframe), the user's dials"},
    {"fluid/FluidEmitters.cpp", "Customize > Hit splats and Radius on hit, the user's (0 is off)"},
    {"fluid/FluidEmitters.hpp", "Customize > Hit splats: its hit edge"},
    {"scenes/FluidScene.cpp", "uBeat for the user's own injection shader (Customize's editor)"},
};

// Scenes that still read the drums on their own. May only shrink.
const std::map<std::string, const char*> kGaps = {
    {"scenes/AcidScene.cpp", "the kick toggle turns the vortex, the kick random re-aims the centre, kick16 swaps the source and nudges the hue"},
    {"scenes/SilkScene.cpp", "kick4 re-seeds the strokes, kick8 jumps the damping, kick16 steps the folds, an onset ends the hush"},
    {"scenes/LifeScene.cpp", "the onset envelope in the solver's rate, kick8 turns the drift, kick16 steps the rule, snare and hat feed it"},
    {"fluid/FluidMath.cpp", "drives(): kick, hat, snare and onset envelopes blended into heat, stir and streak"},
    {"world/MorphDriver.cpp", "the kick envelope as the morph's kick input"},
};

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// Comments out, line breaks kept, so a line number still points at the source.
std::string stripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            if (i < src.size()) out += '\n';
        } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') out += '\n';
                ++i;
            }
            ++i;
        } else {
            out += src[i];
        }
    }
    return out;
}

// The 1-based lines of `code` that match `re`.
std::vector<int> linesMatching(const std::string& code, const std::regex& re) {
    std::vector<int> out;
    std::istringstream lines(code);
    std::string line;
    int n = 0;
    while (std::getline(lines, line)) {
        ++n;
        if (std::regex_search(line, re)) out.push_back(n);
    }
    return out;
}

bool isSource(const fs::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".inc";
}

std::string where(const std::string& file, const std::vector<int>& lines) {
    std::string out = file + ":";
    for (size_t i = 0; i < lines.size() && i < 4; ++i) out += (i ? "," : "") + std::to_string(lines[i]);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path core = argc > 1 ? fs::path(argv[1]) : fs::path("core");
    const fs::path viz = core / "viz";
    if (!fs::is_directory(viz)) {
        std::printf("FAIL: no %s (run from the repo root, or pass the core/ folder)\n", viz.string().c_str());
        return 1;
    }
    const std::regex retiredRead(R"((\.|->)beatResponse\b)");
    const std::regex hitRead(
        R"(live::hit\s*\(|live::Edge|(\.|->)(onsetEnv|onset|transient|transientEnv|transientSharpness|kickEnv|snareEnv|hatEnv|kick|snare|hat|kick4|kick8|kick16|kickToggle|snareToggle|kickRandom)\b)");

    int scanned = 0;
    std::set<std::string> gapsSeen;
    for (const auto& entry : fs::recursive_directory_iterator(core)) {
        if (!entry.is_regular_file() || !isSource(entry.path())) continue;
        const fs::path rel = fs::relative(entry.path(), core);
        if (*rel.begin() == "tests") continue;
        ++scanned;
        const std::string code = stripComments(read(entry.path()));
        const std::string name = rel.generic_string();

        // Rule 1, all of core/.
        const std::vector<int> retired = linesMatching(code, retiredRead);
        check(retired.empty(), where(name, retired) + " reads the retired beatResponse field");

        // Rules 2 and 3, core/viz only: the sound engine (core/sound) is where the hit is made.
        if (*rel.begin() != "viz") continue;
        const std::string inViz = fs::relative(entry.path(), viz).generic_string();
        const std::vector<int> hits = linesMatching(code, hitRead);
        if (hits.empty()) continue;
        if (kAllowed.count(inViz) != 0) continue;
        if (kGaps.count(inViz) != 0) {
            gapsSeen.insert(inViz);
            continue;
        }
        check(false, where("viz/" + inViz, hits) +
                         " reads the hit on its own: route it through the reaction matrix, or put it behind a control the user sets");
    }
    check(scanned > 100, "the scan found core/'s sources (" + std::to_string(scanned) + " files)");
    for (const auto& [file, why] : kGaps) {
        check(gapsSeen.count(file) != 0, "viz/" + file + " no longer reads the hit on its own (" + why + "): take it off kGaps");
    }
    for (const auto& [file, why] : kAllowed) {
        check(fs::exists(viz / file), "viz/" + file + " is allowed (" + why + ") but does not exist");
    }
    if (failures == 0) std::printf("hidden_hits_test: all passed (%d files)\n", scanned);
    return failures == 0 ? 0 : 1;
}
