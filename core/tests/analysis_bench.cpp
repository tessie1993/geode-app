// The analysis accuracy bench (#462): scores every analyser over the lab's labelled clips and fails when a
// score drops below its recorded baseline. Run by .github/workflows/analysis-bench.yml after the lab:
//   python3 tools/analysis_lab/lab.py --out build/analysis-lab
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -Icore core/tests/analysis_bench.cpp core/sound/*.cpp core/sound/offline/*.cpp -o analysis_bench
//   ./analysis_bench build/analysis-lab core/tests/analysis/bench_baseline.tsv [--write <new baseline>]
//
// Analysers, each scored only on what it claims:
//   librosa  the reference (the lab's goldens): beats, tempo, key, sections (oracle-k, see lab.py)
//   live     core/sound's SoundEngine, fed the clip the way export feeds it (exactly the audio up to each
//            60 fps frame, then step(1/60)) and read through its own GeodeSoundFrame: the beat and
//            downbeat triggers (placed at the oscillator's beat, frame time - beatPhase · period),
//            sectionChange as it fires (it means "a section began a bar ago", and the bench scores where
//            it lands, which is what a visual sees), the key and tempo held over the clip's second half
//   offline  core/sound/offline's whole-track analyser (#463): the beat grid, its downbeats and tempo
//
// Metrics are core/tests/analysis/MirEval.hpp (mir_eval's conventions). Before scoring anything the bench
// recomputes librosa's scores and compares them with mir_eval's own (<clip>.mir_eval.tsv): a mismatch fails,
// because then the bench's maths and the field's maths have parted.
//
// The baseline (core/tests/analysis/bench_baseline.tsv) holds one row per analyser, metric and clip plus the
// mean. A score more than the tolerance below its row fails (0.02 for a mean, 0.05 for one clip: one beat
// in twenty); a row the bench no longer produces fails too, so a metric cannot vanish quietly. A score with
// no row yet is reported as new and passes. After a deliberate change, --write prints the new baseline;
// commit it in the same PR so the diff shows what moved. Synthetic clips and pinned librosa make every
// score deterministic on one toolchain; the tolerances absorb libm and compiler differences.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sound/SoundEngine.hpp"
#include "sound/offline/OfflineBeats.hpp"
#include "tests/analysis/Annotations.hpp"
#include "tests/analysis/MirEval.hpp"
#include "tests/analysis/Wav.hpp"

using namespace geode::analysis;

namespace {

constexpr double kMeanTolerance = 0.02;
constexpr double kClipTolerance = 0.05;
constexpr double kMirEvalTolerance = 1e-6;

struct Claims {
    bool beats = false, downbeats = false, tempo = false, key = false, sections = false;
};

// ---- live: core/sound's SoundEngine at 60 fps -------------------------------------------------------------

Answers runLive(const Wav& wav) {
    constexpr int kFps = 60;
    geode::sound::SoundEngine engine(wav.sampleRate);
    GeodeSoundFrame frame{};
    Answers a;
    a.hasDownbeats = a.hasBoundaries = true;
    const int64_t total = static_cast<int64_t>(wav.frames());
    const double duration = static_cast<double>(total) / wav.sampleRate;
    std::vector<float> bpms;
    std::map<int, int> keyVotes;  // root * 2 + major
    int64_t written = 0;
    for (int64_t k = 0;; ++k) {
        const int64_t end = std::min(total, (k + 1) * wav.sampleRate / kFps);
        if (end <= written) break;
        engine.write(wav.interleaved.data() + written * wav.channels, static_cast<int>(end - written), wav.channels);
        written = end;
        engine.step(1.0f / kFps, frame);
        const double t = static_cast<double>(end) / wav.sampleRate;
        const double period = frame.bpm > 0.0f ? 60.0 / frame.bpm : 0.0;
        const double beatAt = t - frame.beatPhase * period;
        if (frame.beat > 0.0f) a.beats.push_back(beatAt);
        if (frame.downbeat > 0.0f) a.downbeats.push_back(beatAt);
        if (frame.sectionChange > 0.0f) a.boundaries.push_back(t);
        if (t >= duration / 2) {
            if (frame.bpm > 0.0f) bpms.push_back(frame.bpm);
            if (frame.keyClarity > 0.0f && frame.keyMajor != 0.5f) {
                const int root = static_cast<int>(std::lround(frame.keyRoot * 12.0f)) % 12;
                ++keyVotes[root * 2 + (frame.keyMajor > 0.5f ? 1 : 0)];
            }
        }
    }
    if (!bpms.empty()) {
        std::nth_element(bpms.begin(), bpms.begin() + bpms.size() / 2, bpms.end());
        a.tempo = bpms[bpms.size() / 2];
    }
    int best = -1;
    for (const auto& [key, votes] : keyVotes)
        if (best < 0 || votes > keyVotes[best]) best = key;
    if (best >= 0) {
        a.hasKey = true;
        a.keyRoot = best / 2;
        a.keyMajor = best % 2 == 1;
    }
    return a;
}

// ---- offline: core/sound/offline, the whole track at once -------------------------------------------------

Answers runOffline(const Wav& wav) {
    const std::vector<float> mono = wav.mono();
    const geode::sound::offline::BeatGrid grid = geode::sound::offline::analyse(mono.data(), mono.size(), wav.sampleRate);
    Answers a;
    a.hasDownbeats = true;
    a.tempo = grid.bpm;
    a.beats = grid.beats;
    a.downbeats = grid.downbeats;
    return a;
}

// ---- Scoring ---------------------------------------------------------------------------------------------

using Scores = std::map<std::string, double>;  // metric -> score

Scores score(const Answers& truth, const Answers& est, const Claims& claims) {
    Scores s;
    if (claims.beats) s["beat_f"] = beatF(truth.beats, est.beats);
    if (claims.downbeats) s["downbeat_f"] = beatF(truth.downbeats, est.downbeats);
    if (claims.tempo) {
        s["tempo_acc1"] = tempoAcc1(truth.tempo, est.tempo);
        s["tempo_acc2"] = tempoAcc2(truth.tempo, est.tempo);
    }
    if (claims.key && truth.hasKey)
        s["key_weighted"] = est.hasKey ? keyWeighted(truth.keyRoot, truth.keyMajor, est.keyRoot, est.keyMajor) : 0.0;
    if (claims.sections && truth.hasBoundaries) {
        s["boundary_f_0.5"] = boundaryF(truth.boundaries, est.boundaries, 0.5);
        s["boundary_f_3"] = boundaryF(truth.boundaries, est.boundaries, 3.0);
    }
    return s;
}

struct Row {
    std::string analyser, metric, clip;
    double value;
};

std::string key(const std::string& analyser, const std::string& metric, const std::string& clip) {
    return analyser + "\t" + metric + "\t" + clip;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <lab dir> <baseline.tsv> [--write <out.tsv>]\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::string baselinePath = argv[2];
    const char* writePath = argc >= 5 && std::strcmp(argv[3], "--write") == 0 ? argv[4] : nullptr;

    bool ok = false;
    const auto clipRows = readRows(dir + "/clips.txt", &ok);
    if (!ok || clipRows.empty()) {
        std::fprintf(stderr, "FAIL: no clips in %s/clips.txt (run tools/analysis_lab/lab.py first)\n", dir.c_str());
        return 1;
    }

    int failures = 0;
    std::vector<Row> rows;
    std::map<std::string, std::vector<double>> perMetric;  // analyser\tmetric -> clip scores
    const Claims librosaClaims{.beats = true, .downbeats = false, .tempo = true, .key = true, .sections = true};
    const Claims liveClaims{.beats = true, .downbeats = true, .tempo = true, .key = true, .sections = true};
    const Claims offlineClaims{.beats = true, .downbeats = true, .tempo = true, .key = false, .sections = false};

    for (const auto& clipRow : clipRows) {
        const std::string clip = clipRow[0];
        const std::string base = dir + "/" + clip;
        bool truthOk = false, refOk = false;
        const Answers truth = readAnswers(base + ".truth.tsv", &truthOk);
        const Answers librosa = readAnswers(base + ".librosa.tsv", &refOk);
        const Wav wav = readWav(base + ".wav");
        if (!truthOk || !refOk || wav.sampleRate == 0) {
            std::fprintf(stderr, "FAIL %s: missing truth, librosa answers or WAV\n", clip.c_str());
            ++failures;
            continue;
        }

        std::vector<std::pair<std::string, Scores>> analysers;
        analysers.emplace_back("librosa", score(truth, librosa, librosaClaims));
        analysers.emplace_back("live", score(truth, runLive(wav), liveClaims));
        analysers.emplace_back("offline", score(truth, runOffline(wav), offlineClaims));

        // The bench's maths against mir_eval's, on the same answers.
        for (const auto& [metric, expected] : readScores(base + ".mir_eval.tsv")) {
            const auto& mine = analysers.front().second;
            const auto it = mine.find(metric);
            if (it == mine.end() || std::fabs(it->second - expected) > kMirEvalTolerance) {
                std::fprintf(stderr, "FAIL %s %s: the bench computes %.9f, mir_eval %.9f\n", clip.c_str(),
                             metric.c_str(), it == mine.end() ? -1.0 : it->second, expected);
                ++failures;
            }
        }

        for (const auto& [analyser, scores] : analysers)
            for (const auto& [metric, value] : scores) {
                rows.push_back({analyser, metric, clip, value});
                perMetric[analyser + "\t" + metric].push_back(value);
            }
    }
    for (const auto& [am, values] : perMetric) {
        double sum = 0.0;
        for (double v : values) sum += v;
        const auto tab = am.find('\t');
        rows.push_back({am.substr(0, tab), am.substr(tab + 1), "mean", sum / values.size()});
    }

    // The table, then the baseline.
    std::printf("%-8s %-15s %-18s %s\n", "analyser", "metric", "clip", "score");
    for (const auto& r : rows) std::printf("%-8s %-15s %-18s %.4f\n", r.analyser.c_str(), r.metric.c_str(), r.clip.c_str(), r.value);

    std::map<std::string, double> baseline;
    for (const auto& r : readRows(baselinePath))
        if (r.size() >= 4) baseline[key(r[0], r[1], r[2])] = std::stod(r[3]);
    std::set<std::string> seen;
    int fresh = 0, better = 0;
    for (const auto& r : rows) {
        const std::string k = key(r.analyser, r.metric, r.clip);
        seen.insert(k);
        const auto it = baseline.find(k);
        if (it == baseline.end()) {
            ++fresh;
            continue;
        }
        const double tolerance = r.clip == "mean" ? kMeanTolerance : kClipTolerance;
        if (r.value < it->second - tolerance) {
            std::fprintf(stderr, "FAIL regression: %s %s %s %.4f < baseline %.4f\n", r.analyser.c_str(),
                         r.metric.c_str(), r.clip.c_str(), r.value, it->second);
            ++failures;
        } else if (r.value > it->second + tolerance) {
            ++better;
        }
    }
    for (const auto& [k, value] : baseline)
        if (!seen.count(k)) {
            std::fprintf(stderr, "FAIL baseline row no longer produced: %s (%.4f)\n", k.c_str(), value);
            ++failures;
        }
    if (fresh) std::printf("%d scores have no baseline row yet (record them with --write)\n", fresh);
    if (better) std::printf("%d scores beat their baseline: record them with --write so they stay won\n", better);

    if (writePath) {
        std::ofstream out(writePath);
        out << "# analyser\tmetric\tclip\tscore — written by analysis_bench --write; see core/tests/analysis_bench.cpp\n";
        char buf[32];
        for (const auto& r : rows) {
            std::snprintf(buf, sizeof buf, "%.4f", r.value);
            out << key(r.analyser, r.metric, r.clip) << "\t" << buf << "\n";
        }
        std::printf("wrote %s\n", writePath);
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("analysis bench: all scores at or above baseline\n");
    return 0;
}
