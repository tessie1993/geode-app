// Host test for the scene params schema (core/viz/Params): what geode_params_schema_text serves.
//   params_schema_test core/tests/params_schema.txt           invariants, then the text against the fixture
//   params_schema_test --write core/tests/params_schema.txt   rewrites the fixture
// The fixture is what ParamSchemaTest and the wire-layout tests read as geode.paramsSchema.
#include <set>

#include "host_check.hpp"
#include "viz/Params.hpp"

using hostcheck::check;

int main(int argc, char** argv) {
    const std::string& text = geode::viz::SceneParams::schemaText();
    const auto table = hostcheck::rows(text);

    check(!table.empty() && table[0][0] == "params" && table[0].size() >= 3, "first row is params|<version>|<count>");
    double declared = -1;
    if (!table.empty() && table[0].size() >= 3) hostcheck::parseNum(table[0][2], declared);
    check(static_cast<int>(declared) == hostcheck::count(table, "param"), "declared count equals the param rows");

    hostcheck::uniqueColumn(table, "param", 2);

    const std::set<std::string> types = {"FLOAT", "BOOL", "INT"};
    int expectedSlot = 0;
    for (const auto& r : table) {
        if (r[0] != "param") continue;
        if (r.size() < 8) {
            check(false, "param row has slot, name, type, default, min, max, fade");
            continue;
        }
        double slot = -1, def = 0, lo = 0, hi = 0;
        check(hostcheck::parseNum(r[1], slot) && static_cast<int>(slot) == expectedSlot, "slots run 0.. in order at " + r[2]);
        ++expectedSlot;
        check(types.count(r[3]) == 1, "known type for " + r[2] + ": " + r[3]);
        check(hostcheck::parseNum(r[4], def) && hostcheck::parseNum(r[5], lo) && hostcheck::parseNum(r[6], hi),
              "numeric default and range for " + r[2]);
        check(lo <= hi, "min <= max for " + r[2]);
        check(def >= lo && def <= hi, "default within range for " + r[2]);
        check(r[7] == "FADE" || r[7] == "HOLD", "fade is FADE or HOLD for " + r[2]);
    }

    return hostcheck::finish(argc, argv, text, "core/tests/params_schema.txt", "params_schema_test");
}
