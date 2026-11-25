#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/Json.hpp"
#include "viz/params/Params.hpp"

// The template file (<name>--<id>.geode.json, a geode://template link's body, a bundled starter): one look plus the
// layout, text and export a video is made with, as the text org.json wrote before the codec moved here. The shell
// hands the file's text in and keeps what comes out; it reads only the identity and the look.
//
// Version tolerance, kept from the first format:
//   * A key this build does not write is carried: each section reads the keys it knows, and whatever the section's
//     writer does not emit for the value just read is kept in its order and written back after the known ones.
//   * A token outside a closed set is carried verbatim (a newer build's ratio, role, quality...), read here as
//     the section's default wherever it is used.
//   * A segment kind this build does not know keeps its name and every field.
//   * A missing or unreadable field takes the default below; a non-finite number takes it too.
//   * formatVersion is written as the larger of this build's and the file's, and never refuses a file.
// Pure; host-tested in core/tests/template_file_test.cpp.
namespace geode::viz::files {

inline constexpr const char* kTemplateFormat = "geode.template";
inline constexpr int kTemplateFormatVersion = 1;

// The keys a section carries for a newer build, in the order the file named them.
using Carried = std::vector<std::pair<std::string, json::Value>>;

struct TemplateLook {
    std::string sceneId;
    float attack = 0.6f;
    float decay = 0.12f;
    SceneParams params;
    std::string customPaletteId;
    std::optional<std::string> customShader;
    Carried foreign;
    Carried paramsForeign;
};

struct TemplateLayout {
    std::string ratio = "R9_16";
    std::string artwork = "BEHIND_BLURRED";
    float artworkScale = 0.62f;
    std::string progress = "BAR";
    float safeArea = 0.06f;
    int accent = -1;
    int backdrop = static_cast<int>(0xFF000000u);
    Carried foreign;
};

struct TemplateSlot {
    std::string role = "CAPTION";
    std::string pattern;
    std::string anchor = "BOTTOM_CENTER";
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float sizeSp = 28.0f;
    int color = -1;
    std::string weight = "MEDIUM";
    bool allCaps = false;
    bool shadow = true;
    Carried foreign;
};

// WHOLE_TRACK, FIXED (startMs, durationMs), LOUDEST_WINDOW (durationMs), or a later kind with its fields.
struct TemplateSegment {
    std::string kind = "WHOLE_TRACK";
    long long startMs = 0;
    long long durationMs = 0;
    Carried fields;
};

struct TemplateExport {
    std::string quality = "FHD1080";
    int fps = 30;
    bool loopSafe = true;
    std::string audio = "FULL_MIX";
    TemplateSegment segment;
    std::string fileNamePattern = "{artist} - {title}";
    Carried foreign;
};

struct VideoTemplate {
    std::string id;
    std::string name;
    TemplateLook look;
    TemplateLayout layout;
    std::vector<TemplateSlot> slots;
    Carried textForeign;
    TemplateExport exportSettings;
    std::string author;
    std::string notes;
    long long createdAtMs = 0;
    int formatVersion = kTemplateFormatVersion;
    Carried foreign;
};

// A template with every section at its default: the catalogue's first style, title over artist bottom left.
VideoTemplate defaultTemplate();

// A fresh id: a random UUID, as java.util.UUID.randomUUID() prints it.
std::string newTemplateId();

enum class TemplateStatus { Parsed, NotATemplate, Malformed };

struct TemplateRead {
    TemplateStatus status = TemplateStatus::Malformed;
    std::string why;  // when not Parsed: what the shell shows
    VideoTemplate value;
};

// The text from its first '{' (a byte order mark or an editor's whitespace before it), its first JSON value only,
// which must be an object whose "format" is geode.template (any case). A blank id takes a fresh one, a blank name
// "Untitled template", a retired style its successor; the envelope is held to the preset ranges. Never throws.
TemplateRead readTemplate(std::string_view text);

// The file text, as org.json's toString(2) wrote it: format, formatVersion, id, name, author, notes, createdAtMs,
// look, layout, text, export, then the carried keys.
std::string writeTemplate(const VideoTemplate& t);

// A geode://template link found in `text` (any case, up to the first whitespace), as the geode://preset link that
// carries the same payload; nullopt when there is none.
std::optional<std::string> templateLinkIn(std::string_view text);
// A geode://preset link as the geode://template link of the same payload; nullopt when it is no preset link or the
// result is longer than `maxLength`.
std::optional<std::string> templateLinkOf(std::string_view presetLink, size_t maxLength);

// The name a shared copy arrives under: \ / : * ? " < > | as '_', trimmed, at most 60 UTF-16 units, trimmed
// again, "Untitled template" when blank, then ".geode.json".
std::string templateShareName(std::string_view name);

}  // namespace geode::viz::files
