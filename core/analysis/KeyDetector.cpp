#include "analysis/KeyDetector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "analysis/Chromagram.hpp"

namespace geode::analysis {

namespace {
constexpr double kMajor[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

void KeyDetector::accumulate(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize) {
    Chromagram::foldPeaks(magnitudes, magnitudeCount, sampleRateHz, fftSize, 60.0f, 5000.0f, frame_.data());
    for (int i = 0; i < 12; i++) chroma_[i] += frame_[i];
    frames_++;
}

std::string KeyDetector::finish() const {
    if (frames_ == 0 || std::all_of(chroma_.begin(), chroma_.end(), [](double v) { return v == 0.0; })) return "";
    double bestScore = -std::numeric_limits<double>::infinity();
    int bestPc = 0;
    bool bestMinor = false;
    for (const bool minor : {false, true}) {
        const double* profile = minor ? kMinor : kMajor;
        for (int root = 0; root < 12; root++) {
            double score = 0.0;
            for (int i = 0; i < 12; i++) score += chroma_[(root + i) % 12] * profile[i];
            score /= norm(chroma_.data()) * norm(profile);
            if (score > bestScore) {
                bestScore = score;
                bestPc = root;
                bestMinor = minor;
            }
        }
    }
    return std::string(Chromagram::kNames[bestPc]) + (bestMinor ? " minor" : " major");
}

std::string KeyDetector::compact(const std::string& key) {
    if (key.empty()) return "";
    if (endsWith(key, " minor")) return key.substr(0, key.size() - 6) + "m";
    if (endsWith(key, " major")) return key.substr(0, key.size() - 6);
    return key;
}

double KeyDetector::norm(const double* v) {
    double sum = 0.0;
    for (int i = 0; i < 12; i++) sum += v[i] * v[i];
    return std::max(std::sqrt(sum), 1e-9);
}

}  // namespace geode::analysis
