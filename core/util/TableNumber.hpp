#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

// How every native text table the shell parses (the params schema, the catalogue presets, the
// modulation-target table) writes a float: the shortest %g form that reads back to exactly v, so
// "0.985", not "0.985000014", and "16777215", not "1.67772e+07", which would read back as another
// seed. One helper, so the tables cannot drift into three spellings of one number.
namespace geode::viz {

inline std::string tableNumber(float v) {
    char buf[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return buf;
}

}  // namespace geode::viz
