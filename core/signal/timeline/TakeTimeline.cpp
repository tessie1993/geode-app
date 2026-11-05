#include "signal/timeline/TakeTimeline.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace geode::signal {

namespace {

constexpr const char* kMagic = "geode-take";

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

std::optional<std::string> unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        if (++i >= s.size()) return std::nullopt;
        if (s[i] == '\\') {
            out += '\\';
        } else if (s[i] == 't') {
            out += '\t';
        } else if (s[i] == 'n') {
            out += '\n';
        } else {
            return std::nullopt;
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(sep, start);
        parts.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) return parts;
        start = at + 1;
    }
}

bool parseInt(const std::string& s, long long& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}

bool parseUnsigned(const std::string& s, unsigned long long& out) {
    if (s.empty() || s[0] == '-') return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtoull(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

// Bit-exact: NaN never equals itself, and a param is never NaN on purpose, but a stored one must not re-record.
bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

}  // namespace

void TakeTimeline::note(int64_t atMs, const std::string& sceneId, const std::string& milkPath, const float* params, int count) {
    std::vector<float> values;
    if (params && count > 0) values.assign(params, params + count);
    if (!choices_.empty() && atMs < choices_.back().atMs) {
        // A seek back: the performance is recorded again from here.
        choices_.erase(std::lower_bound(choices_.begin(), choices_.end(), atMs,
                                        [](const TakeChoice& c, int64_t ms) { return c.atMs < ms; }),
                       choices_.end());
    }
    if (!choices_.empty()) {
        TakeChoice& last = choices_.back();
        if (last.sceneId == sceneId && last.milkPath == milkPath && same(last.params, values)) return;
        // Close on the last one, with the same style: it was a step of the same gesture, so it is replaced. The
        // opening choice and every style change stay their own choices, so StyleSchedule sees every switch.
        const bool sameStyle = last.sceneId == sceneId && last.milkPath == milkPath;
        const bool lastIsAStep = choices_.size() > 1 && choices_[choices_.size() - 2].sceneId == last.sceneId &&
                                 choices_[choices_.size() - 2].milkPath == last.milkPath;
        if (sameStyle && lastIsAStep && atMs - last.atMs < kMinGapMs) {
            last.params = std::move(values);
            return;
        }
    }
    if (full()) return;
    choices_.push_back(TakeChoice{atMs, sceneId, milkPath, std::move(values)});
}

const TakeChoice* TakeTimeline::stateAt(int64_t ms) const {
    if (choices_.empty()) return nullptr;
    const auto it = std::upper_bound(choices_.begin(), choices_.end(), ms,
                                     [](int64_t t, const TakeChoice& c) { return t < c.atMs; });
    return it == choices_.begin() ? &choices_.front() : &*(it - 1);
}

std::vector<const TakeChoice*> TakeTimeline::styleChanges() const {
    std::vector<const TakeChoice*> out;
    for (size_t i = 0; i < choices_.size(); ++i) {
        const TakeChoice& c = choices_[i];
        if (i == 0 || c.sceneId != choices_[i - 1].sceneId || c.milkPath != choices_[i - 1].milkPath) out.push_back(&c);
    }
    return out;
}

std::string TakeTimeline::encode() const {
    std::ostringstream out;
    out << kMagic << ' ' << kVersion << '\n';
    out << "seed " << seed_ << '\n';
    out << "clock " << (songTime_ ? "song" : "wall") << '\n';
    const std::vector<float>* previous = nullptr;
    char number[32];
    for (const TakeChoice& c : choices_) {
        out << "c " << c.atMs << '\t' << escape(c.sceneId) << '\t' << escape(c.milkPath) << '\t' << c.params.size() << '\t';
        // Only what changed since the choice before; every field when the count changed (or for the first).
        const bool whole = previous == nullptr || previous->size() != c.params.size();
        bool first = true;
        for (size_t i = 0; i < c.params.size(); ++i) {
            if (!whole && std::memcmp(&(*previous)[i], &c.params[i], sizeof(float)) == 0) continue;
            std::snprintf(number, sizeof(number), "%.9g", static_cast<double>(c.params[i]));
            out << (first ? "" : ",") << i << ':' << number;
            first = false;
        }
        out << '\n';
        previous = &c.params;
    }
    return out.str();
}

std::optional<TakeTimeline> TakeTimeline::decode(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != std::string(kMagic) + ' ' + std::to_string(kVersion)) return std::nullopt;
    unsigned long long seed = 0;
    bool songTime = true;
    std::vector<TakeChoice> choices;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.rfind("seed ", 0) == 0) {
            if (!parseUnsigned(line.substr(5), seed)) return std::nullopt;
        } else if (line.rfind("clock ", 0) == 0) {
            const std::string clock = line.substr(6);
            if (clock != "song" && clock != "wall") return std::nullopt;
            songTime = clock == "song";
        } else if (line.rfind("c ", 0) == 0) {
            const std::vector<std::string> fields = split(line.substr(2), '\t');
            if (fields.size() != 5) return std::nullopt;
            long long atMs = 0;
            long long count = 0;
            if (!parseInt(fields[0], atMs) || !parseInt(fields[3], count) || count < 0 || count > 4096) return std::nullopt;
            const auto scene = unescape(fields[1]);
            const auto milk = unescape(fields[2]);
            if (!scene || !milk) return std::nullopt;
            TakeChoice c{atMs, *scene, *milk, {}};
            const bool carry = !choices.empty() && choices.back().params.size() == static_cast<size_t>(count);
            c.params = carry ? choices.back().params : std::vector<float>(static_cast<size_t>(count), 0.0f);
            if (!fields[4].empty()) {
                for (const std::string& pair : split(fields[4], ',')) {
                    const size_t colon = pair.find(':');
                    long long index = 0;
                    float value = 0.0f;
                    if (colon == std::string::npos || !parseInt(pair.substr(0, colon), index) || index < 0 || index >= count ||
                        !parseFloat(pair.substr(colon + 1), value)) {
                        return std::nullopt;
                    }
                    c.params[static_cast<size_t>(index)] = value;
                }
            }
            if (!choices.empty() && c.atMs < choices.back().atMs) return std::nullopt;
            choices.push_back(std::move(c));
        } else {
            return std::nullopt;
        }
    }
    if (choices.size() > kMaxChoices) return std::nullopt;
    TakeTimeline t(seed, songTime);
    t.choices_ = std::move(choices);
    return t;
}

}  // namespace geode::signal
