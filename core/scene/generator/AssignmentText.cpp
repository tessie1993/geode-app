#include "scene/generator/AssignmentText.hpp"

#include <array>
#include <utility>

namespace geode::scene::generator {

namespace {

// The next '\t'-separated field of a line, advancing `rest` past it; `more` says whether a tab ended it.
std::string_view field(std::string_view& rest, bool& more) {
    const auto tab = rest.find('\t');
    const std::string_view f = rest.substr(0, tab);
    more = tab != std::string_view::npos;
    rest = more ? rest.substr(tab + 1) : std::string_view{};
    return f;
}

bool validId(std::string_view id) {
    for (char c : id) {
        if (c == '\t' || c == '\n' || c == '\r') return false;
    }
    return true;
}

}  // namespace

std::string assignmentText(const Assignment& assignment) {
    std::string out;
    for (int i = 0; i < kSlotCount; ++i) {
        const SlotState& s = assignment.slots[i];
        out += std::to_string(i);
        out += '\t';
        out += s.id;
        out += '\t';
        out += s.pinned ? '1' : '0';
        out += '\n';
    }
    out += "policy\t";
    out += std::to_string(static_cast<int>(assignment.policy));
    out += '\n';
    return out;
}

bool parseAssignmentText(std::string_view text, Assignment& out) {
    Assignment a;
    std::array<bool, kSlotCount> seen{};
    bool policySeen = false;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        bool more = false;
        const std::string_view head = field(line, more);
        if (head == "policy") {
            if (!more || policySeen) return false;
            const std::string_view n = field(line, more);
            if (more) return false;
            if (n == "0") {
                a.policy = Policy::OnlyMe;
            } else if (n == "1") {
                a.policy = Policy::DirectorSwaps;
            } else {
                return false;
            }
            policySeen = true;
            continue;
        }
        if (head.size() != 1 || head[0] < '0' || head[0] >= static_cast<char>('0' + kSlotCount) || !more) return false;
        const int slot = head[0] - '0';
        if (seen[static_cast<std::size_t>(slot)]) return false;
        const std::string_view id = field(line, more);
        if (!more || !validId(id)) return false;
        const std::string_view pin = field(line, more);
        if (more || (pin != "0" && pin != "1")) return false;
        a.slots[static_cast<std::size_t>(slot)].id = std::string(id);
        a.slots[static_cast<std::size_t>(slot)].pinned = pin == "1";
        seen[static_cast<std::size_t>(slot)] = true;
    }
    for (bool s : seen) {
        if (!s) return false;
    }
    if (!policySeen) return false;
    out = std::move(a);
    return true;
}

}  // namespace geode::scene::generator
