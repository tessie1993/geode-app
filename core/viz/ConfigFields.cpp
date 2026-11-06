// The config params schema's rows (viz/ConfigTable.inc) as the text table the shell reads through
// geode_config_schema_text. The rows themselves are constexpr in viz/Config.hpp; this file only writes
// them out, so the engine code that reads them needs nothing more to link.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "viz/Config.hpp"

namespace geode::viz::config {

namespace {

const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::Float: return "FLOAT";
        case Kind::Int: return "INT";
        case Kind::Bool: return "BOOL";
    }
    return "FLOAT";
}

const char* unitName(Unit unit) {
    switch (unit) {
        case Unit::None: return "NONE";
        case Unit::Seconds: return "SECONDS";
        case Unit::Fraction: return "FRACTION";
        case Unit::Turns: return "TURNS";
        case Unit::Degrees: return "DEGREES";
        case Unit::Points: return "POINTS";
        case Unit::Times: return "TIMES";
    }
    return "NONE";
}

// The shortest %g form that reads back to exactly v, as the scene params text writes its numbers
// (viz/ParamsFields.cpp): "0.005", not "0.00499999989".
std::string number(float v) {
    char buf[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, static_cast<double>(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return buf;
}

}  // namespace

const std::string& schemaText() {
    static const std::string kText = [] {
        std::string out = "configs|" + std::to_string(kTextVersion) + "|" + std::to_string(kCount) + "\n";
        for (int i = 0; i < kCount; ++i) {
            const Slot slot = static_cast<Slot>(i);
            const Spec& s = spec(slot);
            out += std::string("config|") + s.group + "|" + std::to_string(indexInGroup(slot)) + "|" + s.name + "|" +
                   kindName(s.kind) + "|" + number(s.defaultValue) + "|" + number(s.min) + "|" + number(s.max) + "|" +
                   unitName(s.unit) + "\n";
        }
        return out;
    }();
    return kText;
}

}  // namespace geode::viz::config
