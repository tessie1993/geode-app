#include <cmath>
#include <string>

#include "api/geode_responder_api.h"
#include "api/geode_text_out.hpp"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ReactionText.hpp"
#include "viz/modulation/ResponseCatalog.hpp"
#include "viz/modulation/Responder.hpp"
#include "viz/params/Params.hpp"

// The Song events page's words and tiles (viz/modulation/ResponseCatalog) over the same saved reactions text as
// geode_responder_api.cpp: pure, any thread. The edits take the text and hand the new one back.

using geode::api::textOut;

namespace {

namespace resp = geode::viz::responder;
namespace catalog = geode::viz::responder::catalog;

// The rows and responses a saved text holds; no text is a new style's.
resp::Config configOf(const char* text) {
    if (!text) return resp::newStyleConfig();
    resp::Config config;
    geode::viz::reactiontext::decodeResponses(text, config);
    return config;
}

// A response from the page's GEODE_RESP_FIELDS values, each through the edit's own checks (as the preview does).
resp::Response responseOf(const float* values, int count) {
    resp::Config card;
    resp::addResponse(card, resp::Response{});
    if (!values || count < resp::kFieldCount) return card.responses[0];
    constexpr int kRowField = static_cast<int>(resp::Field::Row);
    resp::setField(card, 0, 0, kRowField, values[kRowField]);
    const int row = card.responses[0].row;
    for (int field = 0; field < resp::kFieldCount; ++field) {
        if (field != kRowField) resp::setField(card, row, 0, field, values[field]);
    }
    return card.responses[0];
}

geode::viz::SceneParams lookOf(const float* look) { return look ? geode::viz::SceneParams::fromWire(look) : geode::viz::SceneParams{}; }

}  // namespace

extern "C" {

int geode_resp_catalog_text(char* out, int capacity) try {
    return textOut(catalog::text(), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

int geode_resp_catalog_edit(const char* reactions, int op, int row, int index, float value, char* out, int capacity) try {
    geode::viz::ModDocument doc;
    if (reactions) doc.readReactions(reactions, GEODE_HAND_ALL);
    resp::Config config = configOf(reactions);
    if (std::isfinite(value)) {
        const int number = static_cast<int>(std::lround(value));
        switch (op) {
            case GEODE_RESP_CATALOG_ADD: {
                resp::Response added;
                // A response added is a response wanted: its row comes on (every row still starts off).
                if (catalog::make(number, row, added) && resp::addResponse(config, added) >= 0) resp::setRowOn(config, row, true);
                break;
            }
            case GEODE_RESP_CATALOG_PRESET: {
                const int at = resp::indexOfNth(config, row, index);
                if (at >= 0) catalog::applyPreset(config.responses[static_cast<size_t>(at)], number);
                break;
            }
            default:
                break;
        }
    }
    if (op == GEODE_RESP_CATALOG_RESET_ROW) {
        while (resp::removeResponse(config, row, 0)) {
        }
        resp::setRowOn(config, row, false);
        // The style's own reactions on the row come back too: its chips' mutes are cleared.
        for (int i = config.muteCount - 1; i >= 0; --i) {
            const resp::Mute mute = config.mutes[static_cast<size_t>(i)];
            if (resp::rowOfSource(static_cast<geode::viz::ReactSource>(mute.source)) == row) resp::setMuted(config, mute.source, mute.target, false);
        }
    }
    doc.responses = geode::viz::reactiontext::encodeResponses(config);
    return textOut(doc.reactionsText(), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

int geode_resp_field_text(const float* values, int count, int field, const float* look, char* out, int capacity) try {
    return textOut(catalog::fieldText(responseOf(values, count), field, lookOf(look)), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

int geode_resp_title(const float* values, int count, char* out, int capacity) try {
    return textOut(catalog::title(responseOf(values, count)), out, capacity);
} catch (...) {
    return textOut(std::string(), out, capacity);
}

int geode_resp_preset_of(const float* values, int count) try {
    return catalog::presetOf(responseOf(values, count));
} catch (...) {
    return -1;
}

}  // extern "C"
