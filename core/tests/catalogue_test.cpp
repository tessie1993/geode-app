// Host test for the style catalogue table (core/viz/Catalogue): what geode_catalogue_text serves.
//   catalogue_test core/tests/catalogue.txt           invariants, then the text against the fixture
//   catalogue_test --write core/tests/catalogue.txt   rewrites the fixture
// The fixture is what the JVM tests (CatalogueTest and others) read as geode.catalogue.
#include <set>

#include "host_check.hpp"
#include "viz/Catalogue.hpp"

using hostcheck::check;

int main(int argc, char** argv) {
    const std::string& text = geode::viz::catalogue::text();
    const auto table = hostcheck::rows(text);

    check(!table.empty() && table[0][0] == "catalogue" && table[0].size() >= 2, "first row is catalogue|<version>");
    double version = 0;
    check(table.size() > 1 && hostcheck::parseNum(table[0].size() > 1 ? table[0][1] : "", version) && version >= 1,
          "catalogue version is a number >= 1");

    hostcheck::uniqueColumn(table, "style", 1);
    hostcheck::uniqueColumn(table, "kind", 1);
    hostcheck::uniqueColumn(table, "family", 1);
    check(hostcheck::count(table, "style") > 0, "at least one style");
    check(hostcheck::count(table, "kind") > 0, "at least one kind");

    std::set<std::string> kinds;
    for (const auto& r : table) {
        if (r[0] == "kind" && r.size() >= 2) kinds.insert(r[1]);
    }
    for (const auto& r : table) {
        if (r[0] != "style") continue;
        check(r.size() >= 4, "style row has id, kind, family");
        if (r.size() >= 3) check(kinds.count(r[2]) == 1, "style " + r[1] + " names an unknown kind " + r[2]);
    }
    // Every family names styles that exist.
    std::set<std::string> ids;
    for (const auto& r : table) {
        if (r[0] == "style" && r.size() >= 2) ids.insert(r[1]);
    }
    for (const auto& r : table) {
        if (r[0] != "family" || r.size() < 5) continue;
        for (const auto& id : hostcheck::split(r[4], ',')) {
            check(ids.count(id) == 1, "family " + r[1] + " lists unknown style " + id);
        }
    }

    return hostcheck::finish(argc, argv, text, "core/tests/catalogue.txt", "catalogue_test");
}
