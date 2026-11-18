// The look files' C ABI (core/viz/LookFiles): file names, saved palettes, presets and the template library, as text
// in and text out. Marshals only.
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/LookFiles.hpp"
#include "viz/TemplateFile.hpp"

using geode::viz::SceneParams;
namespace files = geode::viz::files;

namespace {

const char* hold(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.c_str();
}

std::string textOf(const char* text) { return text ? text : ""; }

std::vector<std::string> namesOf(const char* const* names, int count) {
    std::vector<std::string> out;
    for (int i = 0; names && i < count; ++i) out.push_back(textOf(names[i]));
    return out;
}

}  // namespace

extern "C" {

const char* geode_file_safe_name(const char* name) {
    thread_local std::string out;
    return hold(out, files::safeFileName(textOf(name)));
}

const char* geode_file_safe_chars(const char* name) {
    thread_local std::string out;
    return hold(out, files::safeChars(textOf(name)));
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

const char* geode_saved_palette_frame_id(const char* look_id, const float* frame) {
    thread_local std::string out;
    return hold(out, frame ? files::framePaletteId(textOf(look_id), SceneParams::fromWire(frame)) : std::string());
}

int geode_preset_read(const char* text, float* out_frame, int n, const char** out_text, float* out_envelope) {
    if (!out_frame || n < SceneParams::kFieldCount || !out_text || !out_envelope) return 0;
    thread_local files::Preset read;
    const auto p = files::readPreset(textOf(text));
    if (!p) return 0;
    read = *p;
    read.params.toWire(out_frame);
    out_text[0] = read.name.c_str();
    out_text[1] = read.sceneId.c_str();
    out_text[2] = read.customShader ? read.customShader->c_str() : nullptr;
    out_text[3] = read.customPaletteId.c_str();
    out_envelope[0] = read.attack;
    out_envelope[1] = read.decay;
    return 1;
}

const char* geode_preset_write(const char* name, const char* scene_id, float attack, float decay, const char* custom_shader,
                               const float* frame, int n, const char* custom_palette_id) {
    if (!frame || n < SceneParams::kFieldCount) return nullptr;
    thread_local std::string out;
    files::Preset p;
    p.name = textOf(name);
    p.sceneId = textOf(scene_id);
    p.attack = attack;
    p.decay = decay;
    if (custom_shader) p.customShader = custom_shader;
    p.params = SceneParams::fromWire(frame);
    p.customPaletteId = textOf(custom_palette_id);
    return hold(out, files::writePreset(p));
}

int geode_preset_is_built_in(const char* name) { return files::isBuiltInPreset(textOf(name)) ? 1 : 0; }

const char* geode_preset_clean_name(const char* raw, const char* fallback) {
    thread_local std::string out;
    return hold(out, files::cleanPresetName(textOf(raw), textOf(fallback)));
}

const char* geode_preset_folder_name(const char* raw) {
    thread_local std::string out;
    return hold(out, files::cleanFolderName(textOf(raw)));
}

int geode_preset_rename_problem(const char* raw, const char* current, const char* const* taken, int count) {
    return static_cast<int>(files::presetRenameProblem(textOf(raw), textOf(current), namesOf(taken, count)));
}

int geode_preset_folder_name_problem(const char* raw, const char* current, const char* const* folders, int count) {
    const std::optional<std::string> renaming = current ? std::optional<std::string>(current) : std::nullopt;
    return static_cast<int>(files::folderNameProblem(textOf(raw), renaming, namesOf(folders, count)));
}

const char* geode_preset_copy_name(const char* name, const char* const* taken, int count) {
    thread_local std::string out;
    return hold(out, files::presetCopyName(textOf(name), namesOf(taken, count)));
}

const char* geode_preset_import_name(const char* raw, const char* fallback, const char* const* taken, int count) {
    thread_local std::string out;
    return hold(out, files::importedPresetName(textOf(raw), textOf(fallback), namesOf(taken, count)));
}

const char* geode_preset_replace_target(const char* raw, const char* const* names, int count) {
    thread_local std::string out;
    const auto target = files::presetReplaceTarget(textOf(raw), namesOf(names, count));
    return target ? hold(out, *target) : nullptr;
}

int geode_preset_built_in_clash(const char* raw, const char* const* names, int count) {
    return files::builtInNameClash(textOf(raw), namesOf(names, count)) ? 1 : 0;
}

int geode_preset_matches_scene(const char* preset_scene_id, const char* active_scene_id) {
    return files::presetMatchesScene(textOf(preset_scene_id), textOf(active_scene_id)) ? 1 : 0;
}

const char* geode_preset_link_frame(const char* payload) {
    thread_local std::string out;
    return hold(out, files::presetLink(textOf(payload)));
}

int geode_preset_link_fits(const char* link) { return files::presetLinkFits(textOf(link)) ? 1 : 0; }

int geode_preset_link_is(const char* text) { return files::isPresetLink(textOf(text)) ? 1 : 0; }

const char* geode_preset_link_payload(const char* text) {
    thread_local std::string out;
    const auto payload = files::presetLinkPayload(textOf(text));
    return payload ? hold(out, *payload) : nullptr;
}

const char* geode_preset_link_find(const char* text) {
    thread_local std::string out;
    const auto link = files::findPresetLink(textOf(text));
    return link ? hold(out, *link) : nullptr;
}

int geode_preset_link_body_accepted(const char* body) { return files::presetLinkBodyAccepted(textOf(body)) ? 1 : 0; }

int geode_preset_link_max_length(void) { return files::kMaxLinkLength; }

int geode_preset_link_max_inflated(void) { return files::kMaxLinkInflated; }

const char* geode_template_file_name(const char* name, const char* id, const char* suffix) {
    thread_local std::string out;
    return hold(out, files::templateFileName(textOf(name), textOf(id), textOf(suffix)));
}

const char* geode_template_file_tail(const char* id, const char* suffix) {
    thread_local std::string out;
    return hold(out, files::templateFileTail(textOf(id), textOf(suffix)));
}

const char* geode_template_unique_name(const char* base, const char* const* taken, int count) {
    thread_local std::string out;
    std::vector<std::string> names;
    for (int i = 0; taken && i < count; ++i) names.push_back(textOf(taken[i]));
    return hold(out, files::uniqueTemplateName(textOf(base), names));
}

int geode_template_starter_count(void) { return files::starterCount(); }

const char* geode_template_starter(int index) {
    thread_local std::string out;
    return hold(out, std::string(files::starter(index)));
}

int geode_template_is_starter(const char* id) { return files::isStarter(textOf(id)) ? 1 : 0; }

int geode_template_read(const char* text, float* out_frame, int n, const char** out_text, float* out_envelope) {
    if (!out_frame || n < SceneParams::kFieldCount || !out_text || !out_envelope) return -1;
    thread_local files::TemplateRead read;
    thread_local std::string document;
    read = files::readTemplate(textOf(text));
    if (read.status != files::TemplateStatus::Parsed) {
        out_text[0] = read.why.c_str();
        return read.status == files::TemplateStatus::NotATemplate ? 0 : -1;
    }
    document = files::writeTemplate(read.value);
    const files::TemplateLook& look = read.value.look;
    look.params.toWire(out_frame);
    out_text[0] = document.c_str();
    out_text[1] = read.value.id.c_str();
    out_text[2] = read.value.name.c_str();
    out_text[3] = look.sceneId.c_str();
    out_text[4] = look.customShader ? look.customShader->c_str() : nullptr;
    out_text[5] = look.customPaletteId.c_str();
    out_envelope[0] = look.attack;
    out_envelope[1] = look.decay;
    return 1;
}

const char* geode_template_create(const char* name, long long created_at_ms, const char* scene_id, float attack, float decay,
                                  const char* custom_shader, const float* frame, int n,
                                  const char* custom_palette_id) {
    if (!frame || n < SceneParams::kFieldCount) return nullptr;
    thread_local std::string out;
    files::VideoTemplate t = files::defaultTemplate();
    t.id = files::newTemplateId();
    t.name = textOf(name);
    t.createdAtMs = created_at_ms;
    t.look.sceneId = textOf(scene_id);
    t.look.attack = attack;
    t.look.decay = decay;
    if (custom_shader) t.look.customShader = custom_shader;
    t.look.params = SceneParams::fromWire(frame);
    t.look.customPaletteId = textOf(custom_palette_id);
    return hold(out, files::writeTemplate(t));
}

const char* geode_template_renamed(const char* text, const char* name) {
    thread_local std::string out;
    files::TemplateRead read = files::readTemplate(textOf(text));
    if (read.status != files::TemplateStatus::Parsed) return nullptr;
    read.value.name = textOf(name);
    return hold(out, files::writeTemplate(read.value));
}

const char* geode_template_new_copy(const char* text, const char* name, long long created_at_ms) {
    thread_local std::string out;
    files::TemplateRead read = files::readTemplate(textOf(text));
    if (read.status != files::TemplateStatus::Parsed) return nullptr;
    read.value.id = files::newTemplateId();
    read.value.name = textOf(name);
    read.value.author.clear();
    read.value.createdAtMs = created_at_ms;
    return hold(out, files::writeTemplate(read.value));
}

const char* geode_template_link_in(const char* text) {
    thread_local std::string out;
    const auto link = files::templateLinkIn(textOf(text));
    return link ? hold(out, *link) : nullptr;
}

const char* geode_template_link_of(const char* preset_link, int max_length) {
    thread_local std::string out;
    const auto link = files::templateLinkOf(textOf(preset_link), max_length > 0 ? static_cast<size_t>(max_length) : 0);
    return link ? hold(out, *link) : nullptr;
}

const char* geode_template_share_name(const char* name) {
    thread_local std::string out;
    return hold(out, files::templateShareName(textOf(name)));
}

}
