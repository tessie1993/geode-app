#include "shell/StyleShelf.hpp"

#include <algorithm>

namespace geode::shell {

namespace {

bool has(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

bool styleShelfIsFavourite(const StyleShelf& shelf, const std::string& id) { return has(shelf.favourites, id); }

StyleShelf styleShelfApply(const StyleShelf& shelf, const StyleShelfEvent& event) {
    StyleShelf out = shelf;
    switch (event.kind) {
        case StyleShelfEvent::ToggleFavourite: {
            const auto it = std::find(out.favourites.begin(), out.favourites.end(), event.id);
            if (it != out.favourites.end())
                out.favourites.erase(it);
            else
                out.favourites.push_back(event.id);
            break;
        }
        case StyleShelfEvent::PushRecent: {
            std::vector<std::string> next{event.id};
            for (const std::string& r : shelf.recents)
                if (r != event.id) next.push_back(r);
            if (static_cast<int>(next.size()) > kStyleRecentLimit) next.resize(kStyleRecentLimit);
            out.recents = std::move(next);
            break;
        }
        case StyleShelfEvent::RemoveRecent: {
            const auto it = std::find(out.recents.begin(), out.recents.end(), event.id);
            if (it != out.recents.end()) out.recents.erase(it);
            break;
        }
        case StyleShelfEvent::SetRandomFamilyOnly: out.randomFamilyOnly = event.flag; break;
    }
    return out;
}

std::vector<std::string> styleShelfYours(const StyleShelf& shelf, const std::vector<std::string>& order,
                                         const std::optional<std::string>& onScreen) {
    std::vector<std::string> out;
    auto add = [&](const std::string& id) {
        if (has(order, id) && !has(out, id)) out.push_back(id);
    };
    if (onScreen) add(*onScreen);
    for (const std::string& id : order)
        if (styleShelfIsFavourite(shelf, id)) add(id);
    for (const std::string& id : shelf.recents) add(id);
    return out;
}

}  // namespace geode::shell
