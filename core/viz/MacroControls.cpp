#include <algorithm>
#include <string>
#include <vector>

#include "viz/CustomizeMap.hpp"
#include "viz/MacroBank.hpp"
#include "viz/ModTargets.hpp"

// The macro bank seen from Customize's sliders: which target a slider is, which knobs move it, and a new
// knob made from one. Apart from MacroBank.cpp so the renderer's builds need not link the Customize map.
namespace geode::viz::macrobank {

namespace {

// At most maxBytes of s, cut on a UTF-8 character boundary.
std::string cut(std::string s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t at = maxBytes;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) --at;
    s.resize(at);
    return s;
}

std::string_view stemOf(std::string_view key) {
    if (!key.empty() && key.back() == ')') {
        const size_t open = key.rfind(" (");
        if (open != std::string_view::npos && open > 0) key = key.substr(0, open);
    }
    return key;
}

// stem, or stem with " 2", " 3" ... when a knob other than `except` has that name already.
std::string unique(const Bank& bank, const std::string& stem, int except) {
    const auto taken = [&bank, except](const std::string& n) {
        return std::any_of(bank.knobs.begin(), bank.knobs.end(), [&n, except](const Knob& k) { return k.id != except && k.name == n; });
    };
    std::string chosen = cut(stem, kMaxNameBytes);
    for (int n = 2; taken(chosen) && n <= kMaxKnobs + 1; ++n) {
        const std::string suffix = " " + std::to_string(n);
        chosen = cut(stem, kMaxNameBytes - suffix.size()) + suffix;
    }
    return chosen;
}

}  // namespace

Target targetOfControl(std::string_view key) {
    for (const char* field : customize::fieldsOf(key)) {
        const Target target = Target::ofSlot(SceneParams::slotOf(field));
        if (linkable(target)) return target;
    }
    return {};
}

Bank addKnobFor(const Bank& bank, std::string_view key) {
    Bank out = normalized(bank);
    const Target target = targetOfControl(key);
    if (!linkable(target) || !roomForNew(out)) return out;
    out = addKnob(out, unique(out, std::string(stemOf(key)), 0));
    return addLink(out, out.knobs.back().id, target);
}

std::string autoName(const Bank& bank, int id) {
    const Bank valid = normalized(bank);
    const auto at = std::find_if(valid.knobs.begin(), valid.knobs.end(), [id](const Knob& k) { return k.id == id; });
    if (at == valid.knobs.end()) return {};
    for (const Link& link : at->links) {
        const std::string_view control = controlOf(link.target);
        if (!control.empty() && control.front() != '@') return unique(valid, std::string(stemOf(control)), id);
    }
    return unique(valid, "Knob " + std::to_string(id), id);
}

std::string controlsText(const Bank& bank) {
    const Bank valid = normalized(bank);
    std::string out = "macrocontrols|" + std::to_string(kTextVersion) + "\n";
    std::vector<std::string_view> seen;
    for (const customize::Section& section : customize::sections()) {
        for (const char* key : section.controls) {
            const std::string_view k(key);
            if (k.empty() || k.front() == '@' || std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
            seen.push_back(k);
            const Target target = targetOfControl(k);
            if (!linkable(target)) continue;
            std::string ids;
            for (const Knob& knob : valid.knobs) {
                if (!knob.shown) continue;
                if (std::none_of(knob.links.begin(), knob.links.end(), [target](const Link& l) { return l.target == target; })) continue;
                if (!ids.empty()) ids += ",";
                ids += std::to_string(knob.id);
            }
            out += std::string("control|") + key + "|" + name(target) + "|" + customize::name(section.category) + "|" + ids + "\n";
        }
    }
    return out;
}

}  // namespace geode::viz::macrobank
