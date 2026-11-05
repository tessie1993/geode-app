#pragma once
// The bench's metrics, written to mir_eval's conventions (mir_eval 0.8.2, as tools/analysis_lab pins) so a
// Geode number means what a MIR paper's number means:
//   beats, downbeats   mir_eval.beat.f_measure after mir_eval.beat.trim_beats: events before 5 s dropped on
//                      both sides (the tracker's warm-up is not scored), a hit within ±70 ms
//   boundaries         mir_eval.segment.detection with trim=True: the track's start and end are not
//                      boundaries, a hit within ±0.5 s and, as the lenient figure, ±3 s
//   key                mir_eval.key.weighted_score (MIREX): same key 1, a fifth above 0.5, relative 0.3,
//                      parallel 0.2
//   tempo              Acc1 (within 4 %) and Acc2 (also at ×2, ×½, ×3, ×⅓), the usual tempo accuracies
// The lab writes mir_eval's own scores of librosa's answers; analysis_bench recomputes them here and fails
// if the two disagree, so this file cannot drift from mir_eval unnoticed.
#include <algorithm>
#include <cmath>
#include <vector>

namespace geode::analysis {

inline std::vector<double> trimBeats(const std::vector<double>& beats, double minTime = 5.0) {
    std::vector<double> out;
    for (double b : beats)
        if (b >= minTime) out.push_back(b);
    return out;
}

// The largest set of one-to-one pairs with |ref - est| <= window. On a line with one window, taking the
// earliest unmatched candidate on both sides is optimal, so this equals mir_eval.util.match_events'
// bipartite matching.
inline int matchEvents(std::vector<double> ref, std::vector<double> est, double window) {
    std::sort(ref.begin(), ref.end());
    std::sort(est.begin(), est.end());
    size_t i = 0, j = 0;
    int hits = 0;
    while (i < ref.size() && j < est.size()) {
        const double d = est[j] - ref[i];
        if (std::fabs(d) <= window) {
            ++hits;
            ++i;
            ++j;
        } else if (d < 0) {
            ++j;
        } else {
            ++i;
        }
    }
    return hits;
}

inline double fMeasure(const std::vector<double>& ref, const std::vector<double>& est, double window) {
    if (ref.empty() || est.empty()) return 0.0;
    const double hits = matchEvents(ref, est, window);
    const double precision = hits / static_cast<double>(est.size());
    const double recall = hits / static_cast<double>(ref.size());
    return precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
}

inline double beatF(const std::vector<double>& ref, const std::vector<double>& est) {
    return fMeasure(trimBeats(ref), trimBeats(est), 0.07);
}

// Internal boundaries only, on both sides (detection's trim=True).
inline double boundaryF(const std::vector<double>& ref, const std::vector<double>& est, double window) {
    return fMeasure(ref, est, window);
}

inline double keyWeighted(int refRoot, bool refMajor, int estRoot, bool estMajor) {
    const int up = ((estRoot - refRoot) % 12 + 12) % 12;
    if (up == 0 && refMajor == estMajor) return 1.0;
    if (refMajor == estMajor && up == 7) return 0.5;
    if (refMajor && !estMajor && up == 9) return 0.3;
    if (!refMajor && estMajor && up == 3) return 0.3;
    if (refMajor != estMajor && up == 0) return 0.2;
    return 0.0;
}

inline double tempoAcc1(double ref, double est) { return ref > 0 && std::fabs(est - ref) <= 0.04 * ref ? 1.0 : 0.0; }

inline double tempoAcc2(double ref, double est) {
    for (double k : {1.0, 2.0, 0.5, 3.0, 1.0 / 3.0})
        if (tempoAcc1(ref * k, est) > 0.0) return 1.0;
    return 0.0;
}

}  // namespace geode::analysis
