// The look files' C ABI (core/viz/LookFiles): file names, saved palettes, presets and the template library, as text
// in and text out. Marshals only.
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/LookFiles.hpp"

using geode::viz::SceneParams;
namespace files = geode::viz::files;

namespace {

const char* hold(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.c_str();
}

std::string textOf(const char* text) { return text ? text : ""; }

}  // namespace

extern "C" {

const char* geode_file_safe_name(const char* name) {
    thread_local std::string out;
    return hold(out, files::safeFileName(textOf(name)));
}

int geode_looks_order(const char* const* names, int count, int caseless, int* out) {
    if (!names || !out || count <= 0) return 0;
    std::vector<std::string> list;
    for (int i = 0; i < count; ++i) list.push_back(textOf(names[i]));
    const std::vector<int> sorted = files::order(list, caseless != 0);
    for (size_t i = 0; i < sorted.size(); ++i) out[i] = sorted[i];
    return static_cast<int>(sorted.size());
}

const char* geode_saved_palette_read(const char* text) {
    thread_local std::string out;
    const auto p = files::readPalette(textOf(text));
    return p ? hold(out, files::writePalette(*p)) : nullptr;
}

const char* geode_saved_palette_sanitize(const char* text) {
    thread_local std::string out;
    const auto p = files::readPalette(textOf(text));
    return p ? hold(out, files::writePalette(files::sanitized(*p))) : nullptr;
}

const char* geode_saved_palette_create(const char* name, float base, float span, const int* stops, int count) {
    thread_local std::string out;
    files::SavedPalette p;
    p.name = textOf(name);
    p.id = files::paletteIdFor(p.name);
    p.baseHue = base;
    p.hueSpan = span;
    if (stops && count > 0) p.stops.assign(stops, stops + count);
    return hold(out, files::writePalette(files::sanitized(p)));
}

const char* geode_saved_palette_migrate(const char* text) {
    thread_local std::string out;
    const auto migrated = files::migratedPalette(textOf(text));
    return migrated ? hold(out, *migrated) : nullptr;
}

const char* geode_saved_palette_id_for(const char* name) {
    thread_local std::string out;
    return hold(out, files::paletteIdFor(textOf(name)));
}

const char* geode_saved_palette_string(const char* text, const char* key) {
    thread_local std::string out;
    const auto p = files::readPalette(textOf(text));
    const std::string which = textOf(key);
    if (!p) return hold(out, "");
    return hold(out, which == "id" ? p->id : which == "name" ? p->name : std::string());
}

int geode_saved_palette_hues(const char* text, float* out) {
    const auto p = files::readPalette(textOf(text));
    if (!p || !out) return 0;
    out[0] = p->baseHue;
    out[1] = p->hueSpan;
    return 1;
}

int geode_saved_palette_stops(const char* text, int* out) {
    const auto p = files::readPalette(textOf(text));
    if (!p) return 0;
    for (size_t i = 0; out && i < p->stops.size(); ++i) out[i] = p->stops[i];
    return static_cast<int>(p->stops.size());
}

int geode_saved_palette_max_stops(void) { return geode::viz::colour::kMaxStops; }

int geode_saved_palette_apply(const char* text, const float* in, float* out) {
    if (!in || !out) return 0;
    const SceneParams params = SceneParams::fromWire(in);
    const auto p = files::readPalette(textOf(text));
    (p ? files::applyPalette(params, *p) : params).toWire(out);
    return p ? 1 : 0;
}

const char* geode_saved_palette_kept_id(const char* look_id, const char* deleted_id) {
    thread_local std::string out;
    return hold(out, files::keptPaletteId(textOf(look_id), textOf(deleted_id)));
}

}
