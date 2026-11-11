// Host test for the Customize map table (core/viz/CustomizeMap): what geode_customize_text serves.
//   customize_test core/tests/customize.txt           invariants, then the text against the fixture
//   customize_test --write core/tests/customize.txt   rewrites the fixture
// The fixture is what CustomizeMapTableTest reads as geode.customize.
#include <set>

#include "host_check.hpp"
#include "viz/CustomizeMap.hpp"

using hostcheck::check;

int main(int argc, char** argv) {
    const std::string& text = geode::viz::customize::text();
    const auto table = hostcheck::rows(text);

    check(!table.empty() && table[0][0] == "customize" && table[0].size() >= 2, "first row is customize|<version>");
    hostcheck::uniqueColumn(table, "category", 1);
    check(hostcheck::count(table, "category") > 0, "at least one category");

    std::set<std::string> categories;
    for (const auto& r : table) {
        if (r[0] == "category" && r.size() >= 2) categories.insert(r[1]);
    }
    std::set<std::string> sectionKeys;
    std::set<std::string> withSection;
    for (const auto& r : table) {
        if (r[0] != "section") continue;
        check(r.size() >= 6, "section row has category, key, tier, gate, control");
        if (r.size() < 6) continue;
        check(categories.count(r[1]) == 1, "section " + r[2] + " names an unknown category " + r[1]);
        check(sectionKeys.insert(r[1] + "/" + r[2]).second, "duplicate section " + r[1] + "/" + r[2]);
        withSection.insert(r[1]);
    }
    for (const auto& c : categories) check(withSection.count(c) == 1, "category " + c + " has no sections");

    return hostcheck::finish(argc, argv, text, "core/tests/customize.txt", "customize_test");
}
