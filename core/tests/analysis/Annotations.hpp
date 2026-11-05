#pragma once
// The lab's TSV files (tools/analysis_lab/lab.py): one row per value, its kind first.
//   duration <s> | tempo <bpm> | key <root 0..11> <major|minor> | beat <s> | downbeat <s> | boundary <s>
//   kick <s> | snare <s> | hat <s> | drop <s> (a drop's first beat, #465)
// The same reader takes a truth file (<clip>.truth.tsv), librosa's answers (<clip>.librosa.tsv) and any
// analyser's answers the bench builds itself, so every analyser is scored through one shape.
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace geode::analysis {

struct Answers {
    double duration = 0.0;
    double tempo = 0.0;  // 0: no estimate
    bool hasKey = false;
    int keyRoot = 0;
    bool keyMajor = true;
    bool hasDownbeats = false;  // an analyser that does not claim downbeats is not scored on them
    bool hasBoundaries = false;
    bool hasDrops = false;  // a truth file claims drops only where its song has one
    std::vector<double> beats;
    std::vector<double> downbeats;
    std::vector<double> boundaries;
    std::vector<double> kicks, snares, hats;  // a truth file lists a drum only if the clip plays it
    std::vector<double> drops;
};

inline std::vector<std::vector<std::string>> readRows(const std::string& path, bool* ok = nullptr) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(path);
    if (ok) *ok = static_cast<bool>(in);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) cells.push_back(cell);
        if (!cells.empty()) rows.push_back(cells);
    }
    return rows;
}

// A file with no downbeat or boundary rows still claims them when it says so: truth files always do.
inline Answers readAnswers(const std::string& path, bool* ok = nullptr) {
    Answers a;
    for (const auto& r : readRows(path, ok)) {
        const std::string& kind = r[0];
        const double v = r.size() > 1 ? std::stod(r[1]) : 0.0;
        if (kind == "duration") a.duration = v;
        else if (kind == "tempo") a.tempo = v;
        else if (kind == "beat") a.beats.push_back(v);
        else if (kind == "downbeat") a.downbeats.push_back(v), a.hasDownbeats = true;
        else if (kind == "boundary") a.boundaries.push_back(v), a.hasBoundaries = true;
        else if (kind == "kick") a.kicks.push_back(v);
        else if (kind == "snare") a.snares.push_back(v);
        else if (kind == "hat") a.hats.push_back(v);
        else if (kind == "drop") a.drops.push_back(v), a.hasDrops = true;
        else if (kind == "key" && r.size() > 2) {
            a.hasKey = true;
            a.keyRoot = static_cast<int>(v);
            a.keyMajor = r[2] == "major";
        }
    }
    return a;
}

inline std::map<std::string, double> readScores(const std::string& path) {
    std::map<std::string, double> out;
    for (const auto& r : readRows(path))
        if (r.size() > 1) out[r[0]] = std::stod(r[1]);
    return out;
}

}  // namespace geode::analysis
