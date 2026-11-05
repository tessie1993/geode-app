#include "output/RenderEta.hpp"

#include <algorithm>
#include <cmath>

namespace geode::output {

int64_t renderEtaSample(RenderEtaState& s, float progress, int64_t atMs, bool steady, const RenderEtaTuning& t) {
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    if (!steady || clamped < s.latestProgress) {
        s = RenderEtaState{};
        s.latestProgress = clamped;
        return -1;
    }
    s.latestProgress = clamped;
    if (clamped >= 1.0f) return 0;
    if (!s.steady) {
        s.steady = true;
        s.steadySinceMs = atMs;
    }
    if (!s.based) {
        if (atMs - s.steadySinceMs < t.warmupMs) return -1;
        s.based = true;
        s.baseMs = atMs;
        s.baseProgress = clamped;
        return -1;
    }
    const int64_t spanMs = atMs - s.baseMs;
    const float gained = clamped - s.baseProgress;
    if (spanMs < t.minSpanMs || gained < t.minProgress) return -1;
    const float fresh = (1.0f - clamped) * static_cast<float>(spanMs) / gained / 1000.0f;
    float next = fresh;
    if (s.shown) {
        const int64_t dtMs = std::max<int64_t>(atMs - s.shownAtMs, 0);
        const float countedDown = std::max(s.shownSeconds - static_cast<float>(dtMs) / 1000.0f, 0.0f);
        const float pull = 1.0f - std::exp(-static_cast<float>(dtMs) / static_cast<float>(t.smoothingMs));
        next = countedDown + (fresh - countedDown) * pull;
    }
    s.shown = true;
    s.shownSeconds = next;
    s.shownAtMs = atMs;
    // Kotlin's Float.roundToLong: half rounds up.
    const auto rounded = static_cast<int64_t>(std::floor(next + 0.5f));
    return std::max<int64_t>(rounded, 0);
}

EtaPhrase renderEtaPhrase(int64_t secondsLeft, int64_t* count) {
    int64_t n = 0;
    EtaPhrase phrase = EtaPhrase::AlmostDone;
    if (secondsLeft < 30) {
        phrase = EtaPhrase::AlmostDone;
    } else if (secondsLeft < 90) {
        phrase = EtaPhrase::AboutAMinute;
    } else if (secondsLeft < 3600) {
        phrase = EtaPhrase::Minutes;
        n = (secondsLeft + 30) / 60;
    } else {
        phrase = EtaPhrase::Hours;
        n = (secondsLeft + 1800) / 3600;
    }
    if (count) *count = n;
    return phrase;
}

}  // namespace geode::output
