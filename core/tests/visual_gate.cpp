// The visual quality gate (#470): judges the stills tools/glslcheck renders of every style and fails on a
// mostly black picture, blown highlights, or a flash the app's own limiter would have to hold. Built and
// run by CI ("Visual gate" in .github/workflows/android.yml) with the system compiler; no GL here.
//
//   node tools/glslcheck/check.mjs --stills out/stills.tsv           renders (glslcheck README, "Stills gate")
//   ./visual_gate core/tests/visual_gate_baseline.txt out/stills*.tsv judges, exit 1 on a new failure
//   ./visual_gate --write core/tests/visual_gate_baseline.txt out/stills*.tsv
//                                                                     rewrites the baseline on purpose
//
// Why a gate: the Morph World PRs (#314, #348) reported these same numbers by hand (black pixel fraction,
// max and mean brightness) because nothing else looked at the pixels. A style that renders black on a
// groove, clips to flat white, or strobes on the beat is broken for the user however clean it compiles,
// and only a device ever showed it. This makes the look a check every PR runs.
//
// What is judged, per style:
//   black  a still is black when more than kBlackStillFraction of its pixels are under a display luma of
//          0.02 (page.html frameStats). The style fails when at least half its stills are black: one dark
//          moment (the build's dip) is music, half the song dark is a broken style.
//   blown  a still is blown when more than kBlownStillFraction of its pixels have all three channels past
//          0.95. Fails on the same half-the-stills rule.
//   flash  the run's frames (60 fps, the end of the build into the drop) go through FlashLimiter::observe
//          exactly as OutputProbe's grids do on the phone, ungraded. Fails when any one-second window
//          reaches FlashLimiter::kEngageTransitions: on the phone the limiter would engage and squeeze the
//          picture toward grey, on a plain groove, which is a style the user sees going flat. WCAG's own
//          line (more than three flashes a second) is printed beside it. The constants are the limiter's,
//          read from FlashLimiter.hpp, so this can never disagree with what the app does.
//   render the style failed to compile, link or draw under glslcheck.
//
// The baseline (core/tests/visual_gate_baseline.txt) lists the styles that fail a check today and why, so
// the gate landed green and only a new failure fails a PR. An entry that now passes is reported as stale
// (not failed: a look near a threshold may flicker across it between toolchains), to be removed by hand.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "viz/FlashLimiter.hpp"

using namespace geode::viz::safety;

namespace {

// More than this fraction of black pixels makes a still black. Morph World's darkest seed world was 13 %
// black; a style built on thin lines over black sits near 90 %; this is "nothing is on screen".
constexpr float kBlackStillFraction = 0.92f;
// More than this fraction of clipped-white pixels makes a still blown: a quarter of the frame flat white.
constexpr float kBlownStillFraction = 0.25f;

const char* const kChecks[] = {"black", "blown", "flash", "render"};

struct Still {
    double time = 0.0;
    float mean = 0.0f, max = 0.0f, black = 0.0f, blown = 0.0f;
};

struct Frame {
    double time = 0.0;
    std::vector<uint8_t> grid;
};

struct Style {
    std::string id;
    std::string error;
    std::vector<Still> stills;
    std::vector<Frame> frames;
};

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(line);
    while (std::getline(in, cur, sep)) out.push_back(cur);
    return out;
}

bool hexGrid(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) return false;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); i++) {
        unsigned v = 0;
        if (std::sscanf(hex.c_str() + i * 2, "%2x", &v) != 1) return false;
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

// Reads one glslcheck --stills TSV into `styles`. False (with a message) when the file is not one, or was
// written for a probe grid this build's FlashLimiter does not read.
bool readStills(const char* path, std::vector<Style>& styles) {
    std::ifstream in(path);
    if (!in) {
        std::printf("cannot read %s\n", path);
        return false;
    }
    std::string line;
    if (!std::getline(in, line)) {
        std::printf("%s is empty\n", path);
        return false;
    }
    const auto head = split(line, '\t');
    if (head.size() < 6 || head[0] != "stills" || head[1] != "1") {
        std::printf("%s is not a glslcheck --stills file (version 1)\n", path);
        return false;
    }
    if (std::stoi(head[5]) != kProbeGrid) {
        std::printf("%s was boxed to a %s grid; FlashLimiter reads %d\n", path, head[5].c_str(), kProbeGrid);
        return false;
    }
    const size_t gridBytes = static_cast<size_t>(kProbeGrid) * kProbeGrid * 4;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto f = split(line, '\t');
        if (f[0] == "style" && f.size() >= 3) {
            Style s;
            s.id = f[1];
            if (f[2] == "error") s.error = f.size() > 3 ? f[3] : "render error";
            styles.push_back(std::move(s));
        } else if (f[0] == "still" && f.size() >= 6 && !styles.empty()) {
            Still st;
            st.time = std::stod(f[1]);
            st.mean = std::stof(f[2]);
            st.max = std::stof(f[3]);
            st.black = std::stof(f[4]);
            st.blown = std::stof(f[5]);
            styles.back().stills.push_back(st);
        } else if (f[0] == "frame" && f.size() >= 3 && !styles.empty()) {
            Frame fr;
            fr.time = std::stod(f[1]);
            if (!hexGrid(f[2], fr.grid) || fr.grid.size() != gridBytes) {
                std::printf("%s: a frame of %s is not a %d x %d RGBA8 grid\n", path, styles.back().id.c_str(), kProbeGrid,
                            kProbeGrid);
                return false;
            }
            styles.back().frames.push_back(std::move(fr));
        } else {
            std::printf("%s: unreadable line: %.60s\n", path, line.c_str());
            return false;
        }
    }
    return true;
}

// baseline: `<style id> <check>[,<check>...]`, `#` starts a comment (the reason).
std::map<std::string, std::set<std::string>> readBaseline(const char* path, bool& ok) {
    std::map<std::string, std::set<std::string>> out;
    std::ifstream in(path);
    ok = static_cast<bool>(in);
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream words(line);
        std::string id, checks;
        if (!(words >> id)) continue;
        words >> checks;
        for (const auto& c : split(checks, ',')) {
            if (!c.empty()) out[id].insert(c);
        }
    }
    return out;
}

struct Verdict {
    std::set<std::string> fails;
    int blackStills = 0, blownStills = 0;
    float worstBlack = 0.0f, worstBlown = 0.0f, meanLo = 1.0f, meanHi = 0.0f;
    int transitions = 0;
    double transitionsAt = 0.0;
};

Verdict judge(const Style& s) {
    Verdict v;
    if (!s.error.empty()) {
        v.fails.insert("render");
        return v;
    }
    for (const Still& st : s.stills) {
        if (st.black > kBlackStillFraction) v.blackStills++;
        if (st.blown > kBlownStillFraction) v.blownStills++;
        v.worstBlack = std::max(v.worstBlack, st.black);
        v.worstBlown = std::max(v.worstBlown, st.blown);
        v.meanLo = std::min(v.meanLo, st.mean);
        v.meanHi = std::max(v.meanHi, st.mean);
    }
    const int half = (static_cast<int>(s.stills.size()) + 1) / 2;
    if (!s.stills.empty() && v.blackStills >= half) v.fails.insert("black");
    if (!s.stills.empty() && v.blownStills >= half) v.fails.insert("blown");
    // The phone's measurement, untouched by any grade (scale 1), so every swing the style draws counts.
    FlashLimiter limiter;
    for (const Frame& fr : s.frames) {
        limiter.observe(measureGrid(fr.grid.data(), kProbeGrid), Grade{}, fr.time);
        const int n = limiter.transitionsInWindow(fr.time);
        if (n > v.transitions) {
            v.transitions = n;
            v.transitionsAt = fr.time;
        }
    }
    if (v.transitions >= FlashLimiter::kEngageTransitions) v.fails.insert("flash");
    return v;
}

std::string joined(const std::set<std::string>& s) {
    std::string out;
    for (const char* c : kChecks) {
        if (s.count(c) == 0) continue;
        if (!out.empty()) out += ',';
        out += c;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    int arg = 1;
    const bool write = arg < argc && std::string(argv[arg]) == "--write";
    if (write) arg++;
    if (argc - arg < 2) {
        std::printf("usage: visual_gate [--write] <baseline.txt> <stills.tsv>...\n");
        return 2;
    }
    const char* baselinePath = argv[arg++];
    std::vector<Style> styles;
    for (; arg < argc; arg++) {
        if (!readStills(argv[arg], styles)) return 2;
    }
    if (styles.empty()) {
        std::printf("no styles in the stills: the render step did not run\n");
        return 2;
    }

    std::printf("visual gate: %zu styles; black still > %.2f, blown still > %.2f (fail at half the stills); "
                "flash fails at %d transitions in %.0f s (FlashLimiter engages; WCAG allows 3 flashes, 6 transitions)\n",
                styles.size(), kBlackStillFraction, kBlownStillFraction, FlashLimiter::kEngageTransitions,
                static_cast<double>(FlashLimiter::kWindowSeconds));

    bool baselineFound = false;
    const auto baseline = readBaseline(baselinePath, baselineFound);
    if (!baselineFound && !write) std::printf("note: no baseline at %s, every failure is new\n", baselinePath);

    std::map<std::string, std::set<std::string>> current;
    std::vector<std::string> newFailures;
    std::set<std::string> seen;
    for (const Style& s : styles) {
        seen.insert(s.id);
        const Verdict v = judge(s);
        const auto known = baseline.count(s.id) ? baseline.at(s.id) : std::set<std::string>{};
        std::set<std::string> fresh;
        for (const auto& c : v.fails) {
            if (known.count(c) == 0) fresh.insert(c);
        }
        if (!v.fails.empty()) current[s.id] = v.fails;
        const char* status = !fresh.empty() ? "FAIL " : !v.fails.empty() ? "known" : "ok   ";
        if (!s.error.empty()) {
            std::printf("%s %-28s render: %s\n", status, s.id.c_str(), s.error.c_str());
        } else {
            std::printf("%s %-28s black %d/%zu (worst %.3f) blown %d/%zu (worst %.3f) mean %.3f..%.3f "
                        "flash %d tr/s (%.1f flashes/s at %.2f s)%s%s\n",
                        status, s.id.c_str(), v.blackStills, s.stills.size(), v.worstBlack, v.blownStills,
                        s.stills.size(), v.worstBlown, v.meanLo, v.meanHi, v.transitions, v.transitions / 2.0,
                        v.transitionsAt, v.fails.empty() ? "" : "  fails: ", joined(v.fails).c_str());
        }
        if (!fresh.empty()) newFailures.push_back(s.id + " " + joined(fresh));
    }

    // Entries for checks that now pass, or for styles no longer rendered: the baseline only shrinks by hand.
    for (const auto& [id, checks] : baseline) {
        for (const auto& c : checks) {
            const bool stillFails = current.count(id) && current.at(id).count(c);
            if (!stillFails) {
                std::printf("stale baseline: %s %s %s - remove it from %s\n", id.c_str(), c.c_str(),
                            seen.count(id) ? "passes now" : "was not rendered", baselinePath);
            }
        }
    }

    if (write) {
        std::ofstream out(baselinePath);
        out << "# The visual gate's baseline (core/tests/visual_gate.cpp): styles that fail a check today.\n"
               "# `<style id> <check>[,<check>]`, checks black, blown, flash, render; `#` starts the reason.\n"
               "# A new failure fails CI; fix the style or add it here with a reason, in the same PR.\n";
        for (const auto& [id, checks] : current) out << id << ' ' << joined(checks) << "  # baselined by --write\n";
        std::printf("wrote %zu entries to %s\n", current.size(), baselinePath);
        return 0;
    }

    if (!newFailures.empty()) {
        std::printf("\n%zu new failure(s). Fix the style, or baseline it with a reason (lines to paste into %s):\n",
                    newFailures.size(), baselinePath);
        for (const auto& f : newFailures) std::printf("%s\n", f.c_str());
        return 1;
    }
    std::printf("\nvisual gate passed: %zu baselined\n", current.size());
    return 0;
}
