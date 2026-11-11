// Host test for the modulation-target table (core/viz/ModTargets): what geode_mod_targets_text serves.
//   mod_targets_test core/tests/mod_targets.txt           invariants, then the text against the fixture
//   mod_targets_test --write core/tests/mod_targets.txt   rewrites the fixture
// The fixture is what ModTableTest reads as geode.modTargets.
#include "host_check.hpp"
#include "viz/ModTargets.hpp"

using hostcheck::check;

int main(int argc, char** argv) {
    const std::string& text = geode::viz::modtargets::text();
    const auto table = hostcheck::rows(text);

    check(!table.empty() && table[0][0] == "modulation" && table[0].size() >= 2, "first row is modulation|<version>");
    check(hostcheck::count(table, "config") == 1, "exactly one config row");
    hostcheck::uniqueColumn(table, "target", 2);
    hostcheck::uniqueColumn(table, "target", 1);
    hostcheck::uniqueColumn(table, "source", 1);
    hostcheck::uniqueColumn(table, "wave", 1);
    check(hostcheck::count(table, "target") > 0, "at least one target");

    int expected = 0;
    for (const auto& r : table) {
        if (r[0] != "target") continue;
        double idx = -1;
        check(r.size() >= 6 && hostcheck::parseNum(r[1], idx) && static_cast<int>(idx) == expected, "target indices run 0.. in order");
        ++expected;
        // target|<index>|<name>|<scope>|<chained slot, -1 for none>|<flag 0 or 1>
        double slot = 0, flag = 0;
        if (r.size() >= 6) {
            check(hostcheck::parseNum(r[4], slot) && slot >= -1, "target " + r[2] + " has a chained slot >= -1");
            check(hostcheck::parseNum(r[5], flag) && (flag == 0 || flag == 1), "target " + r[2] + " has a 0/1 flag");
        }
    }
    // One polarity row per source: the natural polarity each source starts on.
    check(hostcheck::count(table, "polarity") > 0, "polarity rows present");

    return hostcheck::finish(argc, argv, text, "core/tests/mod_targets.txt", "mod_targets_test");
}
