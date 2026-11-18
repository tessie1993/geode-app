#include "viz/LayerPick.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "viz/Catalogue.hpp"

namespace geode::viz::layer_pick {

namespace {

using layer_state::Layer;
using layer_state::Source;

// What the page calls each source, in Source's order (the owner's words: "User chooses. Or director or macro").
constexpr std::array<const char*, layer_state::kSources> kSourceLabels{"You", "Auto Director", "Macro"};

// Keeps the director's layer draw apart from Auto's style draw on the same play seed.
constexpr std::uint64_t kDirectorSalt = 0x1A7E5C0DEull;

// FNV-1a: small, and the same on every compiler, so a style's suggested layer is the same everywhere.
std::uint64_t hashOf(const std::string& s) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

// The Styles page family each style is in (catalogue::pickerFamilies), by its place there; -1 for none.
int familyOf(const std::string& id) {
    static const std::map<std::string, int> kFamilyOf = [] {
        std::map<std::string, int> out;
        const auto& families = catalogue::pickerFamilies();
        for (size_t f = 0; f < families.size(); ++f) {
            for (const std::string& member : families[f].ids) out.emplace(member, static_cast<int>(f));
        }
        return out;
    }();
    const auto it = kFamilyOf.find(id);
    return it == kFamilyOf.end() ? -1 : it->second;
}

}  // namespace

const std::vector<std::string>& pool() {
    static const std::vector<std::string> kPool = [] {
        std::vector<std::string> out;
        for (const std::string& id : catalogue::allIds()) {
            if (catalogue::has(catalogue::Trait::AutoDraw, id)) out.push_back(id);
        }
        return out;
    }();
    return kPool;
}

std::string suggest(const std::string& activeId) {
    const std::string& active = catalogue::resolveId(activeId);
    const catalogue::Look look = catalogue::lookOf(active);
    const int family = familyOf(active);
    std::vector<const std::string*> apart;
    std::vector<const std::string*> alike;
    std::vector<const std::string*> rest;
    for (const std::string& id : pool()) {
        if (id == active) continue;
        rest.push_back(&id);
        if (catalogue::lookOf(id) != look) continue;
        alike.push_back(&id);
        if (familyOf(id) != family) apart.push_back(&id);
    }
    const auto& from = !apart.empty() ? apart : !alike.empty() ? alike : rest;
    if (from.empty()) return {};
    return *from[static_cast<size_t>(hashOf(active) % from.size())];
}

std::string userPick(const std::string& pickedId, const std::string& activeId) {
    const std::string& picked = catalogue::resolveId(pickedId);
    if (!picked.empty() && catalogue::knows(picked) && picked != catalogue::resolveId(activeId)) return picked;
    return suggest(activeId);
}

int macroIndex(float turn, int count, int previous) {
    if (count <= 0) return -1;
    const float t = std::isfinite(turn) ? std::clamp((turn + 1.0f) * 0.5f, 0.0f, 1.0f) : 0.5f;
    const float n = static_cast<float>(count);
    if (previous >= 0 && previous < count) {
        const float band = kDeadBand / n;
        const float lo = static_cast<float>(previous) / n - band;
        const float hi = static_cast<float>(previous + 1) / n + band;
        if (t >= lo && t <= hi) return previous;
    }
    return std::min(static_cast<int>(t * n), count - 1);
}

const std::string& Picker::pick(const Layer& layer, const std::string& activeId, int moments, std::uint64_t seed, float turn) {
    if (!layer.on) {
        reset();
        return picked_;
    }
    const std::string& active = catalogue::resolveId(activeId);
    switch (layer.source) {
        case Source::User:
            picked_ = userPick(layer.sceneId, active);
            break;
        case Source::Director: {
            // The play's seed: a replay draws anew, and an export that pinned the play's seed draws the same film.
            if (!seeded_ || seed != seed_) {
                draw_ = auto_draw::Draw(seed ^ kDirectorSalt);
                seed_ = seed;
                seeded_ = true;
                directed_.clear();
            }
            if (directed_.empty()) {
                directed_ = suggest(active);
            } else if (moments > 0 || directed_ == active) {
                // A new one at each moment, never the one it had or the style on screen while the pool holds another.
                std::vector<const std::string*> choices;
                for (const std::string& id : pool()) {
                    if (id != active && id != directed_) choices.push_back(&id);
                }
                const int k = draw_.pick(static_cast<int>(choices.size()), -1);
                if (k >= 0) directed_ = *choices[static_cast<size_t>(k)];
            }
            picked_ = directed_;
            break;
        }
        case Source::Macro: {
            const auto& all = pool();
            if (!std::isfinite(turn) || all.empty()) {
                macroIndex_ = -1;
                picked_ = suggest(active);
                break;
            }
            const int count = static_cast<int>(all.size());
            macroIndex_ = macroIndex(turn, count, macroIndex_);
            const std::string& at = all[static_cast<size_t>(macroIndex_)];
            // The step that lands on the style on screen draws the next one along instead.
            picked_ = at != active ? at : all[static_cast<size_t>((macroIndex_ + 1) % count)];
            break;
        }
    }
    return picked_;
}

void Picker::reset() {
    picked_.clear();
    directed_.clear();
    seeded_ = false;
    macroIndex_ = -1;
}

const char* sourceLabel(Source source) {
    const int i = static_cast<int>(source);
    return i >= 0 && i < layer_state::kSources ? kSourceLabels[static_cast<size_t>(i)] : "";
}

Editor editor(const Layer& layer, const std::string& activeId, const std::string& shown, int family,
              const std::vector<int>& knobIds) {
    Editor out;
    out.layer = layer_state::normalized(layer);
    const Layer& l = out.layer;
    const std::string& active = catalogue::resolveId(activeId);
    out.shown = !l.on ? std::string() : l.source == Source::User ? userPick(l.sceneId, active) : shown;
    const auto knob = std::find(knobIds.begin(), knobIds.end(), l.knob);
    out.knob = knob == knobIds.end() ? 0 : static_cast<int>(knob - knobIds.begin());
    std::vector<std::vector<std::string>> offered;
    int holdingShown = -1;
    for (const catalogue::PickerFamily& f : catalogue::pickerFamilies()) {
        std::vector<std::string> ids;
        for (const std::string& id : f.ids) {
            if (id != active) ids.push_back(id);
        }
        if (ids.empty()) continue;
        if (holdingShown < 0 && std::find(ids.begin(), ids.end(), out.shown) != ids.end()) {
            holdingShown = static_cast<int>(offered.size());
        }
        out.families.push_back(f.name);
        offered.push_back(std::move(ids));
    }
    const int count = static_cast<int>(offered.size());
    out.family = family >= 0 && family < count ? family : std::max(holdingShown, 0);
    if (count == 0) return out;
    out.styles = std::move(offered[static_cast<size_t>(out.family)]);
    const auto picked = std::find(out.styles.begin(), out.styles.end(), l.sceneId);
    out.style = picked == out.styles.end() ? -1 : static_cast<int>(picked - out.styles.begin());
    return out;
}

}  // namespace geode::viz::layer_pick
