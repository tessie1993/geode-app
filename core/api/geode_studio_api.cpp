#include "api/geode_studio_api.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "studio/StudioEdit.hpp"

struct geode_studio_text {
    std::string text;
};

namespace {

geode_studio_text* made(std::string text) {
    return std::make_unique<geode_studio_text>(geode_studio_text{std::move(text)}).release();
}

std::optional<std::string> textOrNone(const char* text) {
    return text ? std::optional<std::string>(text) : std::nullopt;
}

}  // namespace

extern "C" {

geode_studio_text* geode_studio_edit_read(const char* json) {
    try {
        if (!json) return made(geode::studio::writeEdit(geode::studio::Edit{}));
        const auto edit = geode::studio::readEdit(json);
        return edit ? made(geode::studio::writeEdit(*edit)) : nullptr;
    } catch (...) {
        return nullptr;
    }
}

geode_studio_text* geode_studio_kind_read(const char* json) {
    try {
        if (!json) return made(geode::studio::writeKind(geode::studio::Kind{}));
        const auto kind = geode::studio::readKind(json);
        return kind ? made(geode::studio::writeKind(*kind)) : nullptr;
    } catch (...) {
        return nullptr;
    }
}

geode_studio_text* geode_studio_edit_new(const char* song_uri, const char* song_title, long long song_ms,
                                         const char* scene_id, const char* milk_path, long long render_ms) {
    try {
        std::optional<geode::studio::Song> song;
        if (song_uri) song = geode::studio::Song{song_uri, song_title ? song_title : "", song_ms};
        std::optional<geode::studio::Look> look;
        if (scene_id) look = geode::studio::Look{scene_id, textOrNone(milk_path), render_ms};
        return made(geode::studio::writeEdit(geode::studio::newEdit(song, look, &geode::studio::randomId)));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_studio_text_get(const geode_studio_text* t) { return t ? t->text.c_str() : ""; }

void geode_studio_text_destroy(geode_studio_text* t) { std::unique_ptr<geode_studio_text> owned(t); }

long long geode_studio_min_clip_ms(void) { return geode::studio::kMinClipMs; }

}  // extern "C"
