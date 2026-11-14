#include "viz/LayerState.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

namespace geode::viz::layer_state {

namespace {

constexpr std::array<const char*, kSources> kSourceNames{"USER", "DIRECTOR", "MACRO"};

std::mutex& tableLock() {
    static std::mutex m;
    return m;
}

Layer& stored() {
    static Layer l;
    return l;
}

std::string& shownId() {
    static std::string s;
    return s;
}

// Polled every frame by every renderer, so it is an atomic outside the lock; bumped under the lock, so
// a reader that sees the new value and then takes the lock reads the new layer.
std::atomic<uint64_t>& counter() {
    static std::atomic<uint64_t> c{0};
    return c;
}

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (;;) {
        const size_t bar = line.find('|', start);
        out.push_back(line.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
        if (bar == std::string_view::npos) return out;
        start = bar + 1;
    }
}

std::optional<int> intOf(std::string_view s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

// strtof, not from_chars: the NDK's libc++ has no float from_chars on older API levels.
std::optional<float> floatOf(std::string_view s) {
    if (s.empty()) return std::nullopt;
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size()) return std::nullopt;
    return v;
}

std::optional<bool> flagOf(std::string_view s) {
    if (s == "1") return true;
    if (s == "0") return false;
    return std::nullopt;
}

// The shortest %g that reads back to exactly v (viz/TableNumber's rule, kept here so this file stays free of
// the viz headers its host test does not build).
std::string number(float v) {
    char buf[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return buf;
}

}  // namespace

const char* name(Source source) {
    const int i = static_cast<int>(source);
    return i >= 0 && i < kSources ? kSourceNames[static_cast<size_t>(i)] : "";
}

std::optional<Source> sourceNamed(std::string_view name) {
    for (int i = 0; i < kSources; ++i) {
        if (name == kSourceNames[static_cast<size_t>(i)]) return static_cast<Source>(i);
    }
    return std::nullopt;
}

Layer normalized(const Layer& layer) {
    Layer clean = layer;
    clean.mix = std::isfinite(layer.mix) ? std::clamp(layer.mix, 0.0f, 1.0f) : 0.0f;
    if (clean.blend < 0 || clean.blend >= kBlends) clean.blend = kDefaultBlend;
    if (clean.knob < 0) clean.knob = 0;
    if (static_cast<int>(clean.source) < 0 || static_cast<int>(clean.source) >= kSources) clean.source = Source::User;
    std::erase_if(clean.sceneId, [](char c) { return c == '|' || c == '\n' || c == '\r'; });
    return clean;
}

void set(const Layer& layer) {
    Layer clean = normalized(layer);
    std::lock_guard<std::mutex> guard(tableLock());
    if (stored() == clean) return;
    stored() = std::move(clean);
    counter().fetch_add(1, std::memory_order_acq_rel);
}

Layer get() {
    std::lock_guard<std::mutex> guard(tableLock());
    return stored();
}

uint64_t generation() { return counter().load(std::memory_order_acquire); }

void reset() { set(Layer{}); }

void publishShown(const std::string& sceneId) {
    std::lock_guard<std::mutex> guard(tableLock());
    shownId() = sceneId;
}

std::string shown() {
    std::lock_guard<std::mutex> guard(tableLock());
    return shownId();
}

std::string encode(const Layer& layer) {
    const Layer l = normalized(layer);
    return "layer|" + std::to_string(kTextVersion) + "|" + (l.on ? "1" : "0") + "|" + name(l.source) + "|" + l.sceneId + "|" +
           std::to_string(l.knob) + "|" + std::to_string(l.blend) + "|" + number(l.mix);
}

Layer decode(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    const std::vector<std::string_view> f = fields(text);
    if (f.size() != 8 || f[0] != "layer" || intOf(f[1]) != kTextVersion) return Layer{};
    const auto on = flagOf(f[2]);
    const auto source = sourceNamed(f[3]);
    const auto knob = intOf(f[5]);
    const auto blend = intOf(f[6]);
    const auto mix = floatOf(f[7]);
    if (!on || !source || !knob || !blend || !mix) return Layer{};
    Layer l;
    l.on = *on;
    l.source = *source;
    l.sceneId = std::string(f[4]);
    l.knob = *knob;
    l.blend = *blend;
    l.mix = *mix;
    return normalized(l);
}

Layer edit(const Layer& layer, Field field, float value, std::string_view id) {
    Layer l = normalized(layer);
    if (field == Field::Style) {
        l.sceneId = std::string(id);
        return normalized(l);
    }
    if (!std::isfinite(value)) return l;
    // Held well inside int's range before rounding, so a wild value reads as out of the list rather than overflowing.
    const int whole = static_cast<int>(std::lround(std::clamp(value, -1.0e6f, 1.0e6f)));
    switch (field) {
        case Field::On:
            l.on = value != 0.0f;
            break;
        case Field::Source:
            if (whole >= 0 && whole < kSources) l.source = static_cast<Source>(whole);
            break;
        case Field::Knob:
            l.knob = whole;
            break;
        case Field::Blend:
            l.blend = whole;
            break;
        case Field::Mix:
            l.mix = value;
            break;
        case Field::Style:
            break;
    }
    return normalized(l);
}

bool change(Field field, float value, std::string_view id) {
    std::lock_guard<std::mutex> guard(tableLock());
    Layer next = edit(stored(), field, value, id);
    if (stored() == next) return false;
    stored() = std::move(next);
    counter().fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool Follower::poll(Layer& out) {
    if (pinned_) {
        if (!pinDirty_) return false;
        pinDirty_ = false;
        out = pinnedLayer_;
        return true;
    }
    // The generation is read before the value: a set landing in between is simply seen again next poll.
    const uint64_t g = generation();
    if (seen_ && *seen_ == g) return false;
    seen_ = g;
    out = get();
    return true;
}

void Follower::pin() {
    pinnedLayer_ = get();
    pinned_ = true;
    pinDirty_ = true;
}

void Follower::unpin() {
    if (!pinned_) return;
    pinned_ = false;
    pinDirty_ = false;
    seen_.reset();
}

}  // namespace geode::viz::layer_state
