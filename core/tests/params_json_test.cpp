// Host checks for the scene params object (core/viz/ParamsJson, util/Json's org.json rules): the number text
// org.json wrote, its coercions, the round trip, and the preset fixtures the app's JVM tests used, read in
// place. Built and run by CI (.github/workflows/native-host-tests.yml) with the fixtures' folder as argv[1].
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "util/Json.hpp"
#include "viz/ParamsJson.hpp"
#include "viz/Params.hpp"

using namespace geode;
using viz::ParamKind;
using viz::ParamSlot;
using viz::SceneParams;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

const SceneParams kDefaults{};

std::vector<float> wire(const SceneParams& p) {
    std::vector<float> out(SceneParams::kFieldCount);
    p.toWire(out.data());
    return out;
}

json::Value val(const char* text) { return json::parse(text).value_or(json::Value::null()); }

SceneParams read(const std::string& text) {
    SceneParams p;
    std::string a, b;
    check(viz::readParamsJson(text, p, a, b), "reads: " + text);
    return p;
}

// A compact object's keys and their value text, as written.
std::map<std::string, std::string> entries(const std::string& text) {
    std::map<std::string, std::string> out;
    size_t i = 1;
    const auto stringEnd = [&text](size_t from) {
        for (size_t k = from + 1; k < text.size(); ++k) {
            if (text[k] == '\\') ++k;
            else if (text[k] == '"') return k;
        }
        return text.size();
    };
    while (i < text.size() && text[i] == '"') {
        const size_t keyEnd = stringEnd(i);
        const std::string key = text.substr(i + 1, keyEnd - i - 1);
        const size_t start = keyEnd + 2;
        size_t end = start;
        if (text[start] == '"') end = stringEnd(start) + 1;
        while (end < text.size() && text[end] != ',' && text[end] != '}') ++end;
        out[key] = text.substr(start, end - start);
        i = end + 1;
    }
    return out;
}

// A pretty-printed fixture's top-level keys and their value text, one per line as org.json's toString(2) puts them.
std::map<std::string, std::string> fixtureEntries(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        const size_t q = line.find('"');
        const size_t colon = q == std::string::npos ? q : line.find("\": ", q + 1);
        if (colon == std::string::npos) continue;
        std::string value = line.substr(colon + 3);
        if (!value.empty() && value.back() == ',') value.pop_back();
        out[line.substr(q + 1, colon - q - 1)] = value;
    }
    return out;
}

std::string fixture(const std::string& dir, const char* name) {
    std::ifstream in(dir + "/" + name);
    check(in.good(), std::string("fixture ") + name + " opens");
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

const std::set<std::string> kEnvelope = {"name", "sceneId", "attack", "decay", "customShader", "milkPreset"};

std::set<std::string> liveNamesAndIds() {
    std::set<std::string> out = {viz::kCustomPaletteIdKey, viz::kCustomPalette2IdKey};
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        if (!SceneParams::retired(static_cast<ParamSlot>(slot))) out.insert(SceneParams::schema()[static_cast<size_t>(slot)].name);
    }
    return out;
}

// org.json wrote a scene param float widened to a double: Double.toString's shortest digits, a whole value as a long.
void numbersAreWrittenAsOrgJsonWroteThem() {
    const std::pair<float, const char*> cases[] = {
        {0.3f, "0.30000001192092896"},  {-0.3f, "-0.30000001192092896"}, {1.0f, "1"},
        {-1.0f, "-1"},                  {0.0f, "0"},                     {-0.0f, "-0"},
        {1e-4f, "9.999999747378752E-5"}, {0.001f, "0.0010000000474974513"}, {12345.678f, "12345.677734375"},
        {-2.5f, "-2.5"},                {0.5f, "0.5"},                   {2.03125f, "2.03125"},
        {16777215.0f, "16777215"},      {1e20f, "1.0000000200408773E20"}, {0x1p63f, "9223372036854775807"},
    };
    for (const auto& [f, text] : cases) {
        const std::string got = json::numberText(static_cast<double>(f));
        check(got == text, std::string("number text ") + text + ", got " + got);
    }
    const std::pair<double, const char*> doubles[] = {
        {1.0, "1.0"},       {100.0, "100.0"},       {1e7, "1.0E7"},          {0.001, "0.001"},   {1e-4, "1.0E-4"},
        {1234567.0, "1234567.0"}, {12345678.9, "1.23456789E7"}, {-0.0, "-0.0"}, {2.5, "2.5"},
    };
    for (const auto& [d, text] : doubles) check(json::javaDoubleText(d) == text, std::string("Double.toString ") + text);
}

void stringsAreEscapedAsOrgJsonEscapedThem() {
    check(json::quoted("a/b\"c\\\n\t\b\f\r\x01" "\xC3\xA9") == "\"a\\/b\\\"c\\\\\\n\\t\\b\\f\\r\\u0001\xC3\xA9\"",
          "quoted: slash, quote, backslash, the letter escapes, a control as hex, UTF-8 as it is");
    check(json::quoted("") == "\"\"", "quoted: empty");
}

void valuesCoerceAsOrgJsonOptCoerces() {
    // optInt: Integer, a Long's low 32 bits, a Double or numeric string cast like Java's (int).
    check(json::asInt(val("7")) == 7, "int: 7");
    check(json::asInt(val("2.75")) == 2 && json::asInt(val("-2.75")) == -2, "int: a double truncates");
    check(json::asInt(val("\"3.9\"")) == 3 && json::asInt(val("\" 4 \"")) == 4, "int: a numeric string, trimmed");
    check(json::asInt(val("\"1e1\"")) == 10, "int: a string in exponent form");
    check(json::asInt(val("4294967297")) == 1, "int: a Long keeps its low 32 bits");
    check(json::asInt(val("3e9")) == INT_MAX && json::asInt(val("-3e9")) == INT_MIN, "int: a Double saturates");
    check(json::asInt(val("\"NaN\"")) == 0, "int: NaN casts to 0");
    check(!json::asInt(val("true")) && !json::asInt(val("\"x\"")) && !json::asInt(val("null")), "int: falls back");
    // optBoolean: a bool, or "true"/"false" in any case; never a number.
    check(json::asBoolean(val("true")) == true && json::asBoolean(val("\"TRUE\"")) == true, "bool: true");
    check(json::asBoolean(val("\"False\"")) == false, "bool: a string false");
    check(!json::asBoolean(val("\"yes\"")) && !json::asBoolean(val("1")) && !json::asBoolean(val("null")), "bool: falls back");
    // optDouble: a number, or a string Double.valueOf reads.
    check(json::asDouble(val("1.5")) == 1.5 && json::asDouble(val("\"1.5f\"")) == 1.5, "double: 1.5 and 1.5f");
    check(json::asDouble(val("\"0x1p1\"")) == 2.0 && json::asDouble(val("\" 2 \"")) == 2.0, "double: hex float, trimmed");
    check(json::asDouble(val("\".5\"")) == 0.5 && json::asDouble(val("\"1.\"")) == 1.0, "double: a bare point");
    check(json::asDouble(val("\"-Infinity\"")) == -std::numeric_limits<double>::infinity(), "double: -Infinity");
    const auto zero = json::asDouble(val("-0"));
    check(zero && *zero == 0.0 && !std::signbit(*zero), "double: a whole -0 is a long 0");
    for (const char* bad : {"\"abc\"", "\"0x10\"", "\".\"", "\"1e\"", "\"inf\"", "true", "null", "{}"}) {
        check(!json::asDouble(val(bad)), std::string("double: falls back on ") + bad);
    }
    // optString: String.valueOf of anything but an object or array.
    check(json::asString(val("\"a\"")) == "a" && json::asString(val("5")) == "5", "string: a string and an int");
    check(json::asString(val("2.5")) == "2.5" && json::asString(val("1e10")) == "1.0E10", "string: a double");
    check(json::asString(val("true")) == "true" && json::asString(val("null")) == "null", "string: a bool and null");
    check(json::asString(val("{}")) == "{}" && json::asString(val("[ ]")) == "[]", "string: an object or array as printed");
}

void defaultsAndARandomLookRoundTrip() {
    SceneParams back;
    std::string a = "x", b = "x";
    check(viz::readParamsJson(viz::writeParamsJson(kDefaults, "", ""), back, a, b), "defaults: read");
    check(wire(back) == wire(kDefaults) && a.empty() && b.empty(), "defaults: round trip");

    uint64_t seed = 0x2545F4914F6CDD1DULL;
    const auto next = [&seed] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(seed >> 11) / static_cast<double>(1ULL << 53);
    };
    for (int round = 0; round < 50; ++round) {
        std::vector<float> frame = wire(kDefaults);
        for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
            if (SceneParams::retired(static_cast<ParamSlot>(slot))) continue;
            const auto& row = SceneParams::schema()[static_cast<size_t>(slot)];
            const double u = next();
            float& f = frame[static_cast<size_t>(slot)];
            switch (row.kind) {
                case ParamKind::Bool: f = u < 0.5 ? 0.0f : 1.0f; break;
                case ParamKind::Int: f = static_cast<float>(std::floor(row.min + u * (row.max - row.min + 1.0))); break;
                case ParamKind::Float:
                case ParamKind::Circular: f = static_cast<float>(row.min + u * (row.max - row.min)); break;
            }
            if (f > row.max) f = row.max;
        }
        const std::string idA = "palette-" + std::to_string(round);
        const std::string idB = "a\"/\\\n\xC3\xA9";
        const std::string text = viz::writeParamsJson(frame.data(), idA, idB);
        SceneParams p;
        std::string gotA, gotB;
        check(viz::readParamsJson(text, p, gotA, gotB), "random: read");
        check(wire(p) == frame, "random: every float reads back to the same float, round " + std::to_string(round));
        check(gotA == idA && gotB == idB, "random: the ids read back");
        check(viz::writeParamsJson(p, gotA, gotB) == text, "random: written again, the same text");
    }
}

void keysTypesRetiredUnknownAndSanitize() {
    std::set<std::string> written;
    for (const auto& [key, value] : entries(viz::writeParamsJson(kDefaults, "", ""))) written.insert(key);
    check(written == liveNamesAndIds(), "written keys: the live schema names plus the two palette ids");

    const SceneParams typed = read(R"({"particleShape":2.75,"kaleidoscope":"true","endlessZoomSpeed":"abc",
        "symmetry":"7.9","mirror":1,"speed":"1.5","palette":true})");
    // The retired Kaleidoscope switch and count still read this way, into kaleidoFolds: on ("true") at 7.
    check(typed.particleShape == 2 && typed.kaleidoFolds == 7.0f, "an int reads through optInt");
    check(typed.kaleidoFolds != 1.0f && typed.mirror == kDefaults.mirror, "a flag takes \"true\", not 1");
    check(typed.endlessZoomSpeed == kDefaults.endlessZoomSpeed && typed.speed == 1.5f, "a float takes a numeric string only");
    check(typed.palette == kDefaults.palette, "a bool where an int goes is the default");

    check(wire(read("{}")) == wire(kDefaults), "an empty object reads as the defaults");
    check(wire(read(R"({"name":"e","sceneId":"silk","attack":0.1,"decay":0.2})")) == wire(kDefaults),
          "a preset with only its envelope reads as the defaults");
    check(wire(read(R"({"beatFlash":0.5,"sceneId":"water","nested":{"speed":3}})")) == wire(kDefaults), "unknown keys are ignored");

    // Water's knobs went with Water (Pool replaced it; the envelope's sceneId remap stays the shell's): a look
    // naming them or any retired row loads, the rows keep their defaults and are never written again.
    const SceneParams old = read(R"({"pulse":0.6,"beatResponse":1.9,"waterDepth":0.7,"motionOrbit":0.9,"speed":2})");
    check(old.speed == 2.0f, "a retired key does not stop the read");
    const auto oldWire = wire(old);
    const auto defWire = wire(kDefaults);
    for (ParamSlot r : viz::kRetiredParams) {
        const auto slot = static_cast<size_t>(r);
        check(oldWire[slot] == defWire[slot], std::string("retired ") + SceneParams::schema()[slot].name + " reads as its default");
        check(written.count(SceneParams::schema()[slot].name) == 0, std::string("retired ") + SceneParams::schema()[slot].name + " is not written");
    }

    const SceneParams wild = read(R"({"speed":99,"zoom":-5,"camFov":1e39,"kaleidoFolds":40,"colorShift":1.5,"rotation":"Infinity"})");
    check(wild.speed == 4.0f && wild.zoom == 0.3f && wild.kaleidoFolds == 16.0f, "sanitized: held to the schema ranges");

    // A look saved before kaleidoFolds opens as it was: switched off is 1 whatever its count, on is its count held
    // to 2..16, and a kaleidoFolds the look carries wins over the old keys.
    check(read(R"({"kaleidoscope":false,"symmetry":8})").kaleidoFolds == 1.0f, "an old look switched off is off");
    check(read(R"({"kaleidoscope":true,"symmetry":8})").kaleidoFolds == 8.0f, "an old look switched on keeps its folds");
    check(read(R"({"kaleidoscope":true,"symmetry":0})").kaleidoFolds == 2.0f &&
              read(R"({"kaleidoscope":true,"symmetry":99})").kaleidoFolds == 16.0f,
          "an old count is held to 2..16");
    check(read(R"({"kaleidoscope":true})").kaleidoFolds == static_cast<float>(SceneParams::kDefaultSymmetryFolds),
          "an old switch with no count takes the old default count");
    check(read(R"({"kaleidoscope":true,"symmetry":8,"kaleidoFolds":3})").kaleidoFolds == 3.0f, "kaleidoFolds wins over the old keys");
    check(wild.camFov == kDefaults.camFov && wild.rotation == kDefaults.rotation, "sanitized: an overflow reads as the default");
    check(wild.colorShift == 1.5f, "sanitized: a circular value is left to wrap");

    SceneParams ids;
    std::string a, b;
    viz::readParamsJson(R"({"customPaletteId":5,"customPalette2Id":null})", ids, a, b);
    check(a == "5" && b == "null", "ids read through optString");
    viz::readParamsJson(R"({"customPaletteId":{}})", ids, a, b);
    check(a == "{}" && b.empty(), "an object id as printed, a missing one as none");

    SceneParams untouched;
    untouched.speed = 2.0f;
    for (const char* bad : {"[]", "1", "nonsense", "", "{\"speed\":1,}"}) {
        check(!viz::readParamsJson(bad, untouched, a, b) && untouched.speed == 2.0f, std::string("not an object: ") + bad);
    }
}

void theCApiMatches() {
    const auto frame = wire(kDefaults);
    const char* text = geode_params_write_json(frame.data(), SceneParams::kFieldCount, "p", nullptr);
    check(text && viz::writeParamsJson(kDefaults, "p", "") == text, "write_json: the same text, NULL id as none");
    check(!geode_params_write_json(frame.data(), SceneParams::kFieldCount - 1, "", ""), "write_json: a short frame");
    const int n = SceneParams::kFieldCount;
    std::vector<float> out(SceneParams::kFieldCount, 7.0f);
    const char* a = nullptr;
    const char* b = nullptr;
    check(geode_params_read_json(R"({"speed":2,"customPalette2Id":"q"})", nullptr, nullptr, out.data(), n, &a, &b) == 1,
          "read_json: reads");
    check(out[static_cast<size_t>(ParamSlot::speed)] == 2.0f && a && *a == '\0' && b && std::string(b) == "q", "read_json: frame and ids");
    check(geode_params_read_json("{}", "", "", out.data(), n, nullptr, nullptr) == 1, "read_json: ids may be NULL");
    check(geode_params_read_json(R"({"look":{"params":{"speed":3}}})", "look\nparams", nullptr, out.data(), n, &a, &b) == 1 &&
              out[static_cast<size_t>(ParamSlot::speed)] == 3.0f,
          "read_json: the object at a path");
    check(geode_params_read_json("not json", nullptr, R"({"speed":1.5})", out.data(), n, &a, &b) == 1 &&
              out[static_cast<size_t>(ParamSlot::speed)] == 1.5f,
          "read_json: the fallback when the document is not standard JSON");
    std::vector<float> kept(SceneParams::kFieldCount, 7.0f);
    check(geode_params_read_json("[1]", nullptr, "1", kept.data(), n, &a, &b) == 0 && kept[0] == 7.0f,
          "read_json: no object writes nothing");
}

// org.json's own text first: what it parsed a number as (an int or long literal against a double) decides optInt,
// and a double it printed whole would read back as a long, so the stored text is read when it is standard JSON.
void theStoredTextIsReadAsOrgJsonParsedIt() {
    // Integer up to INT_MAX; a long literal past it keeps its low 32 bits (Long.intValue); a double saturates.
    const std::pair<const char*, int> ints[] = {
        {"2147483647", INT_MAX},          {"2147483648", INT_MIN},  {"3000000000", -1294967296},
        {"-2147483648", INT_MIN},         {"-2147483649", INT_MAX}, {"-3000000000", 1294967296},
        {"3e9", INT_MAX},                 {"3000000000.0", INT_MAX}, {"-3e9", INT_MIN},
        {"2147483648.0", INT_MAX},        {"9223372036854775808", INT_MAX},
    };
    for (const auto& [text, want] : ints) {
        const auto doc = json::parseStandard(std::string(R"({"v":)") + text + "}");
        check(doc && json::asInt(*doc->get("v")) == want, std::string("optInt of ") + text);
    }
    // The same through the reader: reactSeed's range is 0..16777215, so a wrap and a saturation land apart.
    const auto seed = [](const char* text) {
        SceneParams p;
        std::string a, b;
        const std::string doc = std::string(R"({"reactSeed":)") + text + "}";
        check(viz::readParamsJson(doc, "", "", p, a, b), std::string("reads reactSeed ") + text);
        return p.reactSeed;
    };
    check(seed("3000000000") == 0 && seed("-3000000000") == 16777215, "a long reactSeed wraps, then is held to its range");
    check(seed("3e9") == 16777215 && seed("-3e9") == 0, "a double reactSeed saturates, then is held to its range");
    check(seed("2147483647") == 16777215 && seed("2147483648") == 0, "INT_MAX and one past it");

    // A document that is not standard JSON reads the fallback, org.json's own print of the object.
    SceneParams p;
    std::string a, b;
    for (const char* odd : {R"({"speed":2} trailing)", "{'speed':2}", R"({"speed":+2})", R"({"speed":02})", R"({"speed":.5})",
                            R"({"speed":0x2})", R"({"speed":nan})", R"({"speed":TRUE})"}) {
        check(viz::readParamsJson(odd, "", R"({"speed":1.25})", p, a, b) && p.speed == 1.25f, std::string("fallback for ") + odd);
    }
    check(viz::readParamsJson(R"({"look":{"params":{"speed":2}}})", "look\nparams", "{}", p, a, b) && p.speed == 2.0f,
          "a nested params object by its path");
    check(viz::readParamsJson(R"({"look":{"params":5}})", "look\nparams", R"({"speed":1.25})", p, a, b) && p.speed == 1.25f,
          "no object at the path reads the fallback");
    check(!viz::readParamsJson("{", "", "", p, a, b), "neither text an object");
}

// optString of an object or array id is String.valueOf: org.json's compact print, keys in the order stored.
void anObjectOrArrayIdReadsAsOrgJsonPrintsIt() {
    SceneParams p;
    std::string a, b;
    const char* doc = R"({"customPaletteId":{"b":1,"a":[true,null,"x/y",2.5,3.0,1e2],"b":4},"customPalette2Id":["q",{"k":-0.0}]})";
    check(viz::readParamsJson(doc, "", "", p, a, b), "object and array ids read");
    check(a == R"({"b":4,"a":[true,null,"x\/y",2.5,3,100]})", "an object id as org.json prints it, got " + a);
    check(b == R"(["q",{"k":-0}])", "an array id as org.json prints it, got " + b);
    check(viz::readParamsJson(R"({"customPaletteId":[1,NaN],"customPalette2Id":{}})", "", "", p, a, b) && a.empty() && b == "{}",
          "an id org.json cannot print reads as none; an empty object as {}");
}

// The fixtures PresetGoldenTest held the format to, now read by native, the floats compared as floats.
void thePresetFixturesReadAsBefore(const std::string& dir) {
    const std::string currentText = fixture(dir, "current.json");
    SceneParams current;
    std::string a, b;
    check(viz::readParamsJson(currentText, current, a, b), "current.json reads");
    check(a == "palette-a" && b == "palette-b", "current.json: the palette ids");
    check(current.flowCurl == 12.5f, "current.json: flowCurl");
    check(current.paletteStops() == std::array<int, 5>{0xFF8800, 0x2233FF, 0x10E0A0, 0xFFFFFF, 0x000000},
          "current.json: white and black survive the float wire");
    const auto got = wire(current);
    const auto defaults = wire(kDefaults);
    SceneParams stored;
    std::string storedA, storedB;
    check(viz::readParamsJson(currentText, "", "", stored, storedA, storedB) && wire(stored) == got && storedA == a && storedB == b,
          "current.json: its stored text, read as the document, reads the same");
    for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
        if (SceneParams::retired(static_cast<ParamSlot>(slot))) continue;
        check(got[static_cast<size_t>(slot)] != defaults[static_cast<size_t>(slot)],
              std::string("current.json: ") + SceneParams::schema()[static_cast<size_t>(slot)].name + " read as its default");
    }
    // Written by the app: every params key comes back with the same text, booleans as booleans and ints as ints.
    const auto golden = fixtureEntries(currentText);
    const auto written = entries(viz::writeParamsJson(current, a, b));
    std::set<std::string> goldenKeys;
    for (const auto& [key, value] : golden) {
        if (kEnvelope.count(key)) continue;
        goldenKeys.insert(key);
        const auto it = written.find(key);
        check(it != written.end() && it->second == value,
              "current.json: " + key + " " + value + " written as " + (it == written.end() ? "nothing" : it->second));
    }
    std::set<std::string> writtenKeys;
    for (const auto& [key, value] : written) writtenKeys.insert(key);
    check(goldenKeys == writtenKeys, "current.json: the same keys");

    const std::string legacyText = fixture(dir, "legacy.json");
    SceneParams legacy;
    check(viz::readParamsJson(legacyText, legacy, a, b), "legacy.json reads");
    check(legacy.speed == 1.5f && legacy.zoom == 1.25f && legacy.palette == 3, "legacy.json: its values");
    check(legacy.trails && a == "old-palette", "legacy.json: flags and id");
    check(legacy.kaleidoFolds == 8.0f, "legacy.json: its kaleidoscope, on at 8 folds, opens as kaleidoFolds 8");
    check(legacy.flowCurl == kDefaults.flowCurl && legacy.camFly == kDefaults.camFly &&
              legacy.fluidCatchPoints == kDefaults.fluidCatchPoints && legacy.liquidBorn == kDefaults.liquidBorn &&
              legacy.noteDrums == kDefaults.noteDrums,
          "legacy.json: keys it never had are the defaults");
    const std::set<std::string> retired = {"pulse", "shake", "beatResponse", "flash", "glitch", "strobe", "waterWaveSpeed",
                                           "waterDamping", "waterRippleStrength", "waterDepth", "waterSpecular", "waterFlow",
                                           "waterLiquid", "waterLiquidFlow", "waterLiquidFade", "rippleOverlayEnabled",
                                           "rippleOverlayStrength", "rippleOverlaySpecular", "formDrive", "motionOrbit",
                                           "symmetry", "kaleidoscope", "palette2", "paletteMix", "palette2BaseOverride",
                                           "palette2RangeOverride", "colorCycle", "cycleSpeed", "paletteLut"};
    std::set<std::string> retiredNow;
    const auto legacyWire = wire(legacy);
    for (ParamSlot r : viz::kRetiredParams) {
        const auto slot = static_cast<size_t>(r);
        retiredNow.insert(SceneParams::schema()[slot].name);
        check(legacyWire[slot] == defaults[slot], std::string("legacy.json: retired ") + SceneParams::schema()[slot].name);
    }
    check(retiredNow == retired, "the retired rows are the twenty PresetGoldenTest named, the old Kaleidoscope pair, Palette 2's four, "
                                 "the colour cycle's two and Colour map");
    const auto legacyWritten = entries(viz::writeParamsJson(legacy, a, b));
    for (const auto& name : retired) check(legacyWritten.count(name) == 0, "legacy.json: " + name + " not written again");
    check(legacyWritten.count("beatFlash") == 0, "legacy.json: an unknown key is not written");
    for (const auto& [key, value] : fixtureEntries(legacyText)) {
        const auto it = legacyWritten.find(key);
        if (it != legacyWritten.end()) check(it->second == value, "legacy.json: " + key + " keeps its text");
    }

    SceneParams hostile;
    check(viz::readParamsJson(fixture(dir, "hostile.json"), hostile, a, b), "hostile.json reads");
    check(hostile.particleShape == 2 && hostile.trails && hostile.endlessZoomSpeed == kDefaults.endlessZoomSpeed,
          "hostile.json: 2.75 is 2, \"true\" is on, a string where a number goes is the default");
    check(hostile.speed == 4.0f && hostile.zoom == 0.3f, "hostile.json: speed and zoom held to their ranges");
    check(hostile.rotation == kDefaults.rotation && hostile.warp == kDefaults.warp && hostile.camFov == kDefaults.camFov,
          "hostile.json: infinities and NaN read as the default");
    check(hostile.palette == 0 && hostile.reactSeed == 16777215, "hostile.json: ints held to their ranges");
    check(hostile.paletteLut == kDefaults.paletteLut, "hostile.json: the retired colour map is dropped");
    check(hostile.kaleidoFolds == 16.0f, "hostile.json: an old kaleidoscope's count of 40 opens held to 16 folds");
    check(hostile.intensity == 2.0f && hostile.cymaticsFundamental == 40.0f && hostile.flowCurl == 50.0f,
          "hostile.json: floats held to their ranges");
    check(hostile.colorShift == 1.5f, "hostile.json: a circular value wraps where it is drawn");
}

}  // namespace

int main(int argc, char** argv) {
    numbersAreWrittenAsOrgJsonWroteThem();
    stringsAreEscapedAsOrgJsonEscapedThem();
    valuesCoerceAsOrgJsonOptCoerces();
    defaultsAndARandomLookRoundTrip();
    keysTypesRetiredUnknownAndSanitize();
    theCApiMatches();
    theStoredTextIsReadAsOrgJsonParsedIt();
    anObjectOrArrayIdReadsAsOrgJsonPrintsIt();
    thePresetFixturesReadAsBefore(argc > 1 ? argv[1] : "app/src/test/resources/presets");
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("params_json_test: all passed\n");
    return 0;
}
