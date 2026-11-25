#include "api/geode_responder_api.h"

#include <cmath>
#include <string>

#include "api/geode_text_out.hpp"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ModTargets.hpp"
#include "viz/modulation/ReactionText.hpp"
#include "viz/modulation/Responder.hpp"

// The handle-free half of the Event Responder's ABI: a new style's text, a row's name and the edits, which take
// the saved reactions text and hand the new one back. The calls that need the style's routes or a renderer sit
// beside them (geode_viz_api.cpp).

using geode::api::textOut;

namespace {

namespace resp = geode::viz::responder;

static_assert(resp::kRows == GEODE_RESP_ROWS);
static_assert(resp::kKindCount == GEODE_RESP_KINDS);
static_assert(resp::kFieldCount == GEODE_RESP_FIELDS);
static_assert(resp::kListValues == GEODE_RESP_LIST_VALUES);
static_assert(static_cast<int>(resp::Field::Enabled) == GEODE_RESP_ENABLED);
static_assert(static_cast<int>(resp::Field::Row) == GEODE_RESP_ROW);
static_assert(static_cast<int>(resp::Field::Kind) == GEODE_RESP_KIND);
static_assert(static_cast<int>(resp::Field::Target) == GEODE_RESP_TARGET);
static_assert(static_cast<int>(resp::Field::Amount) == GEODE_RESP_AMOUNT);
static_assert(static_cast<int>(resp::Field::Strength) == GEODE_RESP_STRENGTH);
static_assert(static_cast<int>(resp::Field::FadeIn) == GEODE_RESP_FADE_IN);
static_assert(static_cast<int>(resp::Field::Hold) == GEODE_RESP_HOLD);
static_assert(static_cast<int>(resp::Field::FadeOut) == GEODE_RESP_FADE_OUT);
static_assert(static_cast<int>(resp::Field::Unit) == GEODE_RESP_UNIT);
static_assert(static_cast<int>(resp::Field::Drive) == GEODE_RESP_DRIVE);
static_assert(static_cast<int>(resp::Field::EveryNth) == GEODE_RESP_EVERY_NTH);
static_assert(static_cast<int>(resp::Field::Chance) == GEODE_RESP_CHANCE);
static_assert(static_cast<int>(resp::Field::Cooldown) == GEODE_RESP_COOLDOWN);
static_assert(static_cast<int>(resp::Field::P0) == GEODE_RESP_P0);
static_assert(static_cast<int>(resp::Origin::User) == GEODE_RESP_ORIGIN_USER);
static_assert(static_cast<int>(resp::Origin::StyleDefault) == GEODE_RESP_ORIGIN_STYLE);

// The rows and responses a saved text holds; no text is a new style's.
resp::Config configOf(const char* text) {
    if (!text) return resp::newStyleConfig();
    resp::Config config;
    geode::viz::reactiontext::decodeResponses(text, config);
    return config;
}

}  // namespace

extern "C" {

int geode_resp_default_text(char* out, int capacity) try {
    geode::viz::ModDocument doc;
    doc.responses = geode::viz::reactiontext::encodeResponses(resp::newStyleConfig());
    return textOut(doc.reactionsText(), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

const char* geode_resp_row_name(int row) {
    if (row < 0 || row >= resp::kRows) return nullptr;
    // The names are string literals, so each is NUL-terminated.
    return resp::kRowNames[static_cast<size_t>(row)].data();
}

int geode_resp_edit(const char* reactions, int op, int row, int index, int field, float value, char* out, int capacity) try {
    geode::viz::ModDocument doc;
    if (reactions) doc.readReactions(reactions, GEODE_HAND_ALL);
    resp::Config config = configOf(reactions);
    switch (op) {
        case GEODE_RESP_EDIT_ROW_ON:
            if (std::isfinite(value)) resp::setRowOn(config, row, value > 0.5f);
            break;
        case GEODE_RESP_EDIT_ADD: {
            if (!std::isfinite(value)) break;
            const int target = static_cast<int>(std::lround(value));
            if (target <= static_cast<int>(geode::viz::LfoTarget::None) || target >= geode::viz::modtargets::kTargetCount) break;
            resp::Response added;
            added.row = row;
            added.target = static_cast<geode::viz::LfoTarget>(target);
            resp::addResponse(config, added);
            break;
        }
        case GEODE_RESP_EDIT_REMOVE:
            resp::removeResponse(config, row, index);
            break;
        case GEODE_RESP_EDIT_SET:
            resp::setField(config, row, index, field, value);
            break;
        case GEODE_RESP_EDIT_CLEAR:
            config = resp::Config{};
            break;
        case GEODE_RESP_EDIT_STYLE_MUTE:
            if (std::isfinite(value)) resp::setMuted(config, row, index, value > 0.5f);
            break;
        default:
            break;
    }
    doc.responses = geode::viz::reactiontext::encodeResponses(config);
    return textOut(doc.reactionsText(), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

}  // extern "C"
