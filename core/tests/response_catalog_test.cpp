// Host checks for the Song events page's catalogue (core/viz/modulation/ResponseCatalog, api/geode_responder_catalog_api.cpp):
// the families, tiles, kind controls, envelope presets and row names the page reads as text, every tile making a response
// the bank accepts, an amount shown as a percentage of the style's own value, the chips' titles, and the edits that add a
// tile and apply a preset to the saved text. Built and run by CI (native-host-tests.yml); no GL.
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "api/geode_responder_api.h"
#include "viz/modulation/ModCatalog.hpp"
#include "viz/modulation/ReactionText.hpp"
#include "viz/modulation/ResponseCatalog.hpp"
#include "viz/modulation/Responder.hpp"
#include "viz/panel/ParamText.hpp"
#include "viz/params/Params.hpp"

using namespace geode::viz;
namespace resp = geode::viz::responder;
namespace catalog = geode::viz::responder::catalog;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::vector<std::vector<std::string>> lines(const std::string& text, const std::string& kind) {
    std::vector<std::vector<std::string>> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cells;
        std::istringstream row(line);
        std::string cell;
        while (std::getline(row, cell, '|')) cells.push_back(cell);
        if (!cells.empty() && cells[0] == kind) out.push_back(cells);
    }
    return out;
}

std::string edit(const char* text, int op, int row, int index, float value) {
    std::vector<char> out(8192);
    geode_resp_catalog_edit(text, op, row, index, value, out.data(), static_cast<int>(out.size()));
    return out.data();
}

resp::Config read(const std::string& text) {
    resp::Config config;
    reactiontext::decodeResponses(text, config);
    return config;
}

std::string fieldText(const resp::Response& r, int field, const float* look) {
    float values[GEODE_RESP_FIELDS] = {};
    resp::fieldValues(r, values);
    std::vector<char> out(256);
    geode_resp_field_text(values, GEODE_RESP_FIELDS, field, look, out.data(), static_cast<int>(out.size()));
    return out.data();
}

constexpr float kNone = std::numeric_limits<float>::quiet_NaN();

void theTextListsWhatThePageDraws() {
    const std::string& text = catalog::text();
    check(text.rfind("responses|" + std::to_string(catalog::kTextVersion) + "\n", 0) == 0, "the text opens with its version");
    check(lines(text, "family").size() == catalog::kFamilyCount, "seven families");
    check(lines(text, "preset").size() == catalog::kPresetCount, "four envelope presets");
    check(lines(text, "curve").size() == catalog::kCurveCount, "the four strength curves");
    check(lines(text, "drive").size() == resp::kDriveCount, "the four drive modes");
    check(lines(text, "row").size() == resp::kRows, "every row has a name");
    check(lines(text, "entry").size() == static_cast<size_t>(catalog::entryCount()), "every tile is listed");
    check(lines(text, "kind").size() == 5, "five kinds");
    check(lines(text, "common").size() == 6, "the controls every response has");

    // The rows: each Moment once, named, in a category that exists.
    std::vector<bool> seen(resp::kRows, false);
    for (const auto& row : lines(text, "row")) {
        const int at = std::stoi(row[1]);
        check(at >= 0 && at < resp::kRows && !seen[static_cast<size_t>(at)], "a row is listed once");
        if (at >= 0 && at < resp::kRows) seen[static_cast<size_t>(at)] = true;
        check(row[2] == std::string(resp::kRowNames[static_cast<size_t>(at)]) && !row[3].empty(), "a row has its saved name and a label");
        check(std::stoi(row[4]) < static_cast<int>(lines(text, "category").size()), "a row's category exists");
    }
    for (const auto& row : lines(text, "row")) {
        const bool needsBeat = row[2] == "BEAT" || row[2] == "BAR";
        check((row[5] == "1") == needsBeat, "only Beat and Bar are told to need a heard beat");
    }
    // A tile is in a family the text lists.
    for (const auto& e : lines(text, "entry")) check(std::stoi(e[2]) >= 0 && std::stoi(e[2]) < catalog::kFamilyCount, "a tile's family exists");
    for (int family = 0; family < catalog::kFamilyCount; ++family) {
        int n = 0;
        for (const auto& e : lines(text, "entry")) n += std::stoi(e[2]) == family ? 1 : 0;
        check(n > 0, "every family has a tile");
    }
}

void theEnvelopePresetsMapToFadeInHoldAndFadeOut() {
    resp::Response r;
    check(catalog::applyPreset(r, 0) && r.fadeIn == 0.0f && r.hold == 0.0f && r.fadeOut > 0.0f, "Snap is a hard start and a short fade out");
    check(catalog::applyPreset(r, 2) && r.fadeIn > r.fadeOut, "Swell comes in slower than it leaves");
    check(catalog::applyPreset(r, 3) && r.hold > r.fadeIn, "Stay holds");
    for (int i = 0; i < catalog::kPresetCount; ++i) {
        catalog::applyPreset(r, i);
        check(catalog::presetOf(r) == i, "a preset reads back as itself");
    }
    r.fadeIn = 0.77f;
    check(catalog::presetOf(r) == -1, "an envelope of the user's own is no preset");
    check(!catalog::applyPreset(r, 4) && !catalog::applyPreset(r, -1), "no such preset changes nothing");
}

void everyTileMakesAResponseTheBankAccepts() {
    const auto controls = lines(catalog::text(), "control");
    for (int i = 0; i < catalog::entryCount(); ++i) {
        resp::Response r;
        check(catalog::make(i, 3, r), "tile " + std::to_string(i) + " makes a response");
        check(r.row == 3 && r.enabled, "a tile's response lands on the row it is added to");
        check(catalog::presetOf(r) >= 0, "a tile starts on one of the envelope presets");
        if (r.kind == resp::kParam) check(r.target != LfoTarget::None && modcatalog::targetLabel(r.target)[0] != '\0', "a parameter tile moves a named target");
        // Its kind-params sit inside the ranges its kind's controls give.
        for (const auto& c : controls) {
            if (std::stoi(c[1]) != r.kind) continue;
            const int field = std::stoi(c[2]);
            if (field < GEODE_RESP_P0) continue;
            const float v = r.params[static_cast<size_t>(field - GEODE_RESP_P0)];
            check(v >= std::stof(c[5]) - 1e-4f && v <= std::stof(c[6]) + 1e-4f, "a tile's kind-param is inside its control's range");
        }
    }
    resp::Response r;
    check(!catalog::make(-1, 0, r) && !catalog::make(catalog::entryCount(), 0, r) && !catalog::make(0, resp::kRows, r), "no such tile or row makes nothing");

    // Every kind the bank plays has a tile, and no tile is a kind the bank has no handler for.
    bool kinds[5] = {};
    for (int i = 0; i < catalog::entryCount(); ++i) {
        catalog::make(i, 0, r);
        if (r.kind >= 0 && r.kind < 5) kinds[r.kind] = true;
    }
    for (const bool k : kinds) check(k, "each kind has a tile");
}

void anAmountReadsAsAPercentageOfTheStylesOwnValue() {
    resp::Response r;
    r.kind = resp::kParam;
    r.target = static_cast<LfoTarget>(modcatalog::targetNamed("ZOOM"));
    r.amount = 0.5f;
    SceneParams look;
    look.set("zoom", 1.0f);
    float one[SceneParams::kFieldCount] = {};
    look.toWire(one);
    look.set("zoom", 2.0f);
    float two[SceneParams::kFieldCount] = {};
    look.toWire(two);
    const std::string a = fieldText(r, GEODE_RESP_AMOUNT, one);
    const std::string b = fieldText(r, GEODE_RESP_AMOUNT, two);
    check(a == paramText(0.5f, ParamUnit::Percent, kNone, true), "an amount is a percentage of a value of 1");
    check(b == paramText(0.25f, ParamUnit::Percent, kNone, true), "the same amount is half as much of a value of 2");
    check(a != b, "the style's own value changes what the amount reads as");

    // Where the style holds none of its own, the amount reads as its reach.
    r.target = static_cast<LfoTarget>(modcatalog::targetNamed("CAM_SHAKE"));
    SceneParams still;
    still.set("camShake", 0.0f);
    float zero[SceneParams::kFieldCount] = {};
    still.toWire(zero);
    check(fieldText(r, GEODE_RESP_AMOUNT, zero) == paramText(0.5f, ParamUnit::Percent, kNone, true), "no value of its own: the amount's own percentage");
    r.amount = -0.5f;
    check(fieldText(r, GEODE_RESP_AMOUNT, zero).find("\xE2\x88\x92") == 0, "a negative amount carries a true minus");
}

void theWordsOfAResponse() {
    resp::Response r;
    check(fieldText(r, GEODE_RESP_EVERY_NTH, nullptr) == "Every hit", "every hit");
    r.everyNth = 2;
    check(fieldText(r, GEODE_RESP_EVERY_NTH, nullptr) == "Every 2nd hit", "every 2nd hit");
    r.everyNth = 3;
    check(fieldText(r, GEODE_RESP_EVERY_NTH, nullptr) == "Every 3rd hit", "every 3rd hit");
    r.everyNth = 12;
    check(fieldText(r, GEODE_RESP_EVERY_NTH, nullptr) == "Every 12th hit", "every 12th hit");
    r.unit = resp::TimeUnit::Beats;
    r.fadeIn = 1.0f;
    check(fieldText(r, GEODE_RESP_FADE_IN, nullptr) == "1 beat", "a beat-length time reads in beats");
    r.unit = resp::TimeUnit::Seconds;
    r.fadeIn = 0.35f;
    check(fieldText(r, GEODE_RESP_FADE_IN, nullptr) == paramText(0.35f, ParamUnit::Seconds, kNone, false), "a time in seconds");
    r.strength = ModCurve::Smooth;
    check(fieldText(r, GEODE_RESP_STRENGTH, nullptr) == "Smooth", "the strength curve is named");
    r.drive = resp::DriveMode::Follow;
    check(fieldText(r, GEODE_RESP_DRIVE, nullptr) == "Follow", "the drive mode is named");
    check(fieldText(r, 99, nullptr).empty() && fieldText(r, -1, nullptr).empty(), "no such field has no words");

    // A kind's own controls read through their choices and units.
    resp::Response thing;
    check(catalog::make(0, 0, thing) && thing.kind == resp::kThing, "the first tile is a Things response");
    check(fieldText(thing, GEODE_RESP_P0, nullptr) == "Dot", "a shape is named");
    resp::Response clock;
    for (int i = 0; i < catalog::entryCount(); ++i) {
        catalog::make(i, 0, clock);
        if (clock.kind == resp::kClock) break;
    }
    check(clock.kind == resp::kClock && fieldText(clock, GEODE_RESP_P0, nullptr) == "Freeze", "a clock response says what it does");
}

void aChipSaysWhatItDoes() {
    resp::Response r;
    r.kind = resp::kParam;
    r.target = static_cast<LfoTarget>(modcatalog::targetNamed("WARP"));
    float values[GEODE_RESP_FIELDS] = {};
    resp::fieldValues(r, values);
    std::vector<char> out(256);
    geode_resp_title(values, GEODE_RESP_FIELDS, out.data(), static_cast<int>(out.size()));
    check(std::string(out.data()) == modcatalog::targetLabel(r.target), "a parameter chip is named for what it moves");
    catalog::make(0, 0, r);
    resp::fieldValues(r, values);
    geode_resp_title(values, GEODE_RESP_FIELDS, out.data(), static_cast<int>(out.size()));
    check(std::string(out.data()) == "Things: Dot, Drift", "a picture chip names its kind and what it draws");
    check(catalog::title(r) == "Things: Dot, Drift", "title and its ABI agree");
}

void theEditsAddATileAndApplyAPreset() {
    std::string t = edit(nullptr, GEODE_RESP_CATALOG_ADD, 0, 0, 0.0f);
    resp::Config config = read(t);
    check(config.count == 1 && config.responses[0].row == 0 && config.rowOn[0], "a tile is added to its row and the row comes on");
    bool others = false;
    for (int row = 1; row < resp::kRows; ++row) others = others || config.rowOn[static_cast<size_t>(row)];
    check(!others, "no other row comes on");

    const int last = catalog::entryCount() - 1;
    t = edit(t.c_str(), GEODE_RESP_CATALOG_ADD, 0, 0, static_cast<float>(last));
    check(read(t).count == 2, "a second response stacks on the row");
    t = edit(t.c_str(), GEODE_RESP_CATALOG_PRESET, 0, 1, 2.0f);
    config = read(t);
    check(catalog::presetOf(config.responses[1]) == 2, "a preset sets fade in, hold and fade out together");
    float values[GEODE_RESP_FIELDS] = {};
    resp::fieldValues(config.responses[1], values);
    check(geode_resp_preset_of(values, GEODE_RESP_FIELDS) == 2, "the ABI names the preset");

    const std::string before = t;
    t = edit(t.c_str(), GEODE_RESP_CATALOG_ADD, 0, 0, static_cast<float>(catalog::entryCount()));
    t = edit(t.c_str(), GEODE_RESP_CATALOG_ADD, 99, 0, 0.0f);
    t = edit(t.c_str(), GEODE_RESP_CATALOG_PRESET, 0, 9, 1.0f);
    t = edit(t.c_str(), GEODE_RESP_CATALOG_PRESET, 0, 0, 9.0f);
    t = edit(t.c_str(), 99, 0, 0, 0.0f);
    check(t == before, "an edit that names nothing hands back what came in");

    // A style chip muted on the row (and one on another row) before the reset.
    const int kick = static_cast<int>(hand::trigger(Moment::Kick));
    const int snare = static_cast<int>(hand::trigger(Moment::Snare));
    const int zoom = static_cast<int>(LfoTarget::Zoom);
    std::vector<char> muted(8192);
    geode_resp_edit(t.c_str(), GEODE_RESP_EDIT_STYLE_MUTE, kick, zoom, 0, 1.0f, muted.data(), static_cast<int>(muted.size()));
    std::string withMutes = muted.data();
    geode_resp_edit(withMutes.c_str(), GEODE_RESP_EDIT_STYLE_MUTE, snare, zoom, 0, 1.0f, muted.data(), static_cast<int>(muted.size()));
    withMutes = muted.data();
    check(read(withMutes).muteCount == 2, "two style chips are muted");
    t = edit(withMutes.c_str(), GEODE_RESP_CATALOG_RESET_ROW, 0, 0, 0.0f);
    config = read(t);
    check(config.count == 0 && !config.rowOn[0], "a row reset removes its responses and puts it off");
    check(config.muteCount == 1 && config.mutes[0].source == snare, "a row reset unmutes its own style chips and no other row's");

    // The same through the text a page reads.
    std::vector<char> out(32768);
    const int need = geode_resp_catalog_text(out.data(), static_cast<int>(out.size()));
    check(need == static_cast<int>(catalog::text().size()) && std::string(out.data()) == catalog::text(), "the ABI hands the whole catalogue");
}

}  // namespace

int main() {
    theTextListsWhatThePageDraws();
    theEnvelopePresetsMapToFadeInHoldAndFadeOut();
    everyTileMakesAResponseTheBankAccepts();
    anAmountReadsAsAPercentageOfTheStylesOwnValue();
    theWordsOfAResponse();
    aChipSaysWhatItDoes();
    theEditsAddATileAndApplyAPreset();
    if (failures != 0) {
        std::printf("response_catalog_test: %d failed\n", failures);
        return 1;
    }
    std::printf("response_catalog_test: all passed\n");
    return 0;
}
