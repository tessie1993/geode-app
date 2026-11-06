#include "scene/generator/Pool.hpp"

#include <string_view>

#include "viz/Catalogue.hpp"

namespace geode::scene::generator {

namespace {

namespace catalogue = viz::catalogue;

// The simulation kinds a generator may be, compared by the catalogue's kind name (catalogue::name), so
// the text path and the direct path share one rule.
bool isSimulation(std::string_view kind) {
    using catalogue::Kind;
    for (Kind k : {Kind::Silk, Kind::Fluid, Kind::Life, Kind::Acid, Kind::Cymatics}) {
        if (kind == catalogue::name(k)) return true;
    }
    return false;
}

bool isGenerator(std::string_view kind, bool marched) {
    if (kind == catalogue::name(catalogue::Kind::Shader)) return !marched;
    return isSimulation(kind);
}

// The next '|'-separated field of a line, advancing `rest` past it.
std::string_view field(std::string_view& rest) {
    const auto bar = rest.find('|');
    const std::string_view f = rest.substr(0, bar);
    rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
    return f;
}

}  // namespace

std::vector<std::string> generatorPool() {
    std::vector<std::string> out;
    for (const auto& id : catalogue::allIds()) {
        if (isGenerator(catalogue::name(catalogue::kindOf(id)), catalogue::isMarched(id))) out.push_back(id);
    }
    return out;
}

std::vector<std::string> generatorPool(const char* catalogueText) {
    std::vector<std::string> out;
    if (catalogueText == nullptr) return out;
    const std::string_view march = catalogue::name(catalogue::Scope::MarchDetail);
    std::string_view text(catalogueText);
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (field(line) != "style") continue;
        const std::string_view id = field(line);
        const std::string_view kind = field(line);
        bool marched = false;
        while (!line.empty()) marched = field(line) == march || marched;
        if (!id.empty() && isGenerator(kind, marched)) out.emplace_back(id);
    }
    return out;
}

}  // namespace geode::scene::generator
