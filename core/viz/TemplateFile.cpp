#include "viz/TemplateFile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>

#include "viz/Catalogue.hpp"
#include "viz/LookFiles.hpp"
#include "viz/params/ParamsJson.hpp"

namespace geode::viz::files {

namespace {

using json::Value;

constexpr std::string_view kTemplatePrefix = "geode://template/";
constexpr std::string_view kPresetPrefix = "geode://preset/";
constexpr const char* kUntitled = "Untitled template";
constexpr const char* kParamsPath = "look\nparams";
constexpr long long kHookMs = 30000;

constexpr std::array<const char*, 6> kRatios = {"R16_9", "R9_16", "R1_1", "R4_5", "R4_3", "R21_9"};
constexpr std::array<const char*, 4> kArtworks = {"NONE", "BEHIND_BLURRED", "CARD", "CIRCLE"};
constexpr std::array<const char*, 4> kProgress = {"NONE", "BAR", "RING", "WAVEFORM"};
constexpr std::array<const char*, 6> kRoles = {"TITLE", "ARTIST", "ALBUM", "CAPTION", "HANDLE", "CREDIT"};
constexpr std::array<const char*, 9> kAnchors = {"TOP_LEFT",    "TOP_CENTER",   "TOP_RIGHT",
                                                 "MIDDLE_LEFT", "MIDDLE_CENTER", "MIDDLE_RIGHT",
                                                 "BOTTOM_LEFT", "BOTTOM_CENTER", "BOTTOM_RIGHT"};
constexpr std::array<const char*, 3> kWeights = {"REGULAR", "MEDIUM", "BOLD"};
constexpr std::array<const char*, 3> kQualities = {"HD720", "FHD1080", "UHD4K"};
constexpr std::array<const char*, 2> kAudio = {"FULL_MIX", "MUTED"};
constexpr const char* kWhole = "WHOLE_TRACK";
constexpr const char* kFixed = "FIXED";
constexpr const char* kLoudest = "LOUDEST_WINDOW";

bool caselessEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto fold = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; };
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

const Value* objectAt(const Value* source, const char* key) {
    const Value* v = source ? source->get(key) : nullptr;
    return v && v->kind() == Value::Kind::Object ? v : nullptr;
}

// org.json's optString: "" when missing.
std::string textAt(const Value* source, const char* key) {
    const Value* v = source ? source->get(key) : nullptr;
    return v ? json::asString(*v).value_or("") : std::string();
}

// optDouble narrowed to a finite float, else `fallback`.
float floatAt(const Value* source, const char* key, float fallback) {
    if (!source) return fallback;
    const Value* v = source->get(key);
    return finiteOr((v ? json::asDouble(*v) : std::nullopt).value_or(static_cast<double>(fallback)), fallback);
}

bool boolAt(const Value* source, const char* key, bool fallback) {
    const Value* v = source ? source->get(key) : nullptr;
    return (v ? json::asBoolean(*v) : std::nullopt).value_or(fallback);
}

long long longAt(const Value* source, const char* key, long long fallback) {
    const Value* v = source ? source->get(key) : nullptr;
    return (v ? json::asLong(*v) : std::nullopt).value_or(fallback);
}

int intAt(const Value* source, const char* key, int fallback) {
    const Value* v = source ? source->get(key) : nullptr;
    return (v ? json::asInt(*v) : std::nullopt).value_or(fallback);
}

// Present, not null and not empty: a custom shader.
std::optional<std::string> textOrNone(const Value* source, const char* key) {
    const Value* v = source ? source->get(key) : nullptr;
    if (!v || v->isNull()) return std::nullopt;
    std::string text = json::asString(*v).value_or("");
    if (text.empty()) return std::nullopt;
    return text;
}

// A closed-set token: missing or blank is the default, a known one in any case is its own spelling, anything else
// is kept as written (trimmed).
template <size_t N>
std::string tokenAt(const Value* source, const char* key, const std::array<const char*, N>& known, const char* fallback) {
    const std::string raw = trimmed(textAt(source, key));
    if (raw.empty()) return fallback;
    for (const char* name : known) {
        if (caselessEqual(raw, name)) return name;
    }
    return raw;
}

// Kotlin's String.toLongOrNull(16) of "#AARRGGBB" without the '#', as an Int (its low 32 bits).
std::optional<int> hexArgb(std::string_view raw) {
    if (raw.empty()) return std::nullopt;
    bool negative = false;
    size_t i = 0;
    if (raw[0] < '0') {
        if (raw.size() == 1 || (raw[0] != '-' && raw[0] != '+')) return std::nullopt;
        negative = raw[0] == '-';
        i = 1;
    }
    const unsigned long long limit = negative ? (1ULL << 63) : (1ULL << 63) - 1;
    unsigned long long magnitude = 0;
    for (; i < raw.size(); ++i) {
        const char c = raw[i];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) return std::nullopt;
        if (magnitude > (limit - static_cast<unsigned long long>(digit)) / 16) return std::nullopt;
        magnitude = magnitude * 16 + static_cast<unsigned long long>(digit);
    }
    const unsigned long long bits = negative ? (0ULL - magnitude) : magnitude;
    return static_cast<int>(static_cast<uint32_t>(bits & 0xFFFFFFFFULL));
}

int argbAt(const Value* source, const char* key, int fallback) {
    std::string raw = trimmed(textAt(source, key));
    if (!raw.empty() && raw[0] == '#') raw.erase(0, 1);
    return hexArgb(raw).value_or(fallback);
}

std::string argbText(int argb) {
    char out[16];
    std::snprintf(out, sizeof out, "#%08X", static_cast<unsigned>(static_cast<uint32_t>(argb)));
    return out;
}

// ---- Writing ------------------------------------------------------------------------------------------------

class Object {
public:
    Object& put(const std::string& key, Value v) {
        if (members_.count(key) == 0) keys_.push_back(key);
        members_[key] = std::move(v);
        return *this;
    }
    // Carried keys after the known ones; a key this build writes wins.
    Object& merge(const Carried& carried) {
        for (const auto& [key, v] : carried) {
            if (members_.count(key) == 0) put(key, v);
        }
        return *this;
    }
    Value build() const { return Value::object(members_, keys_); }

private:
    std::map<std::string, Value> members_;
    std::vector<std::string> keys_;
};

Value str(const std::string& s) { return Value::string(s); }
Value whole(long long n) { return Value::whole(static_cast<double>(n), n); }
Value number(float v, float fallback) { return Value::number(static_cast<double>(std::isfinite(v) ? v : fallback)); }

// Everything in `source` the writer did not emit, in the source's order. A value org.json could not print (a
// non-finite number) is not carried.
Carried foreignOf(const Value* source, const Value& emitted) {
    Carried out;
    if (!source || source->kind() != Value::Kind::Object) return out;
    for (const std::string& key : source->keys()) {
        if (emitted.get(key)) continue;
        const Value& v = source->object().at(key);
        if (json::orgJsonText(v)) out.emplace_back(key, v);
    }
    return out;
}

Value paramsValue(const TemplateLook& look) {
    const std::optional<Value> params = json::parse(writeParamsJson(look.params, look.customPaletteId, ""));
    Object out;
    if (params) {
        for (const std::string& key : params->keys()) out.put(key, params->object().at(key));
    }
    return out.merge(look.paramsForeign).build();
}

Value lookValue(const TemplateLook& look) {
    Object out;
    out.put("sceneId", str(look.sceneId))
        .put("attack", number(look.attack, 0.6f))
        .put("decay", number(look.decay, 0.12f))
        .put("params", paramsValue(look));
    if (look.customShader) out.put("customShader", str(*look.customShader));
    return out.merge(look.foreign).build();
}

Value layoutValue(const TemplateLayout& layout) {
    return Object()
        .put("ratio", str(layout.ratio))
        .put("artwork", str(layout.artwork))
        .put("artworkScale", number(layout.artworkScale, 0.62f))
        .put("progress", str(layout.progress))
        .put("safeArea", number(layout.safeArea, 0.06f))
        .put("accent", str(argbText(layout.accent)))
        .put("backdrop", str(argbText(layout.backdrop)))
        .merge(layout.foreign)
        .build();
}

Value slotValue(const TemplateSlot& slot) {
    return Object()
        .put("role", str(slot.role))
        .put("pattern", str(slot.pattern))
        .put("anchor", str(slot.anchor))
        .put("offsetX", number(slot.offsetX, 0.0f))
        .put("offsetY", number(slot.offsetY, 0.0f))
        .put("sizeSp", number(slot.sizeSp, 28.0f))
        .put("color", str(argbText(slot.color)))
        .put("weight", str(slot.weight))
        .put("allCaps", Value::boolean(slot.allCaps))
        .put("shadow", Value::boolean(slot.shadow))
        .merge(slot.foreign)
        .build();
}

Value textValue(const std::vector<TemplateSlot>& slots, const Carried& foreign) {
    std::vector<Value> items;
    for (const TemplateSlot& slot : slots) items.push_back(slotValue(slot));
    return Object().put("slots", Value::array(std::move(items))).merge(foreign).build();
}

Value segmentValue(const TemplateSegment& segment) {
    Object out;
    out.put("kind", str(segment.kind));
    if (segment.kind == kWhole) return out.build();
    if (segment.kind == kFixed) return out.put("startMs", whole(segment.startMs)).put("durationMs", whole(segment.durationMs)).build();
    if (segment.kind == kLoudest) return out.put("durationMs", whole(segment.durationMs)).build();
    return out.merge(segment.fields).build();
}

Value exportValue(const TemplateExport& e) {
    return Object()
        .put("quality", str(e.quality))
        .put("fps", whole(e.fps))
        .put("loopSafe", Value::boolean(e.loopSafe))
        .put("audio", str(e.audio))
        .put("segment", segmentValue(e.segment))
        .put("fileNamePattern", str(e.fileNamePattern))
        .merge(e.foreign)
        .build();
}

Value templateValue(const VideoTemplate& t) {
    return Object()
        .put("format", str(kTemplateFormat))
        .put("formatVersion", whole(std::max(kTemplateFormatVersion, t.formatVersion)))
        .put("id", str(t.id))
        .put("name", str(t.name))
        .put("author", str(t.author))
        .put("notes", str(t.notes))
        .put("createdAtMs", whole(t.createdAtMs))
        .put("look", lookValue(t.look))
        .put("layout", layoutValue(t.layout))
        .put("text", textValue(t.slots, t.textForeign))
        .put("export", exportValue(t.exportSettings))
        .merge(t.foreign)
        .build();
}

// ---- Reading ------------------------------------------------------------------------------------------------

std::vector<TemplateSlot> defaultSlots() {
    TemplateSlot title;
    title.role = "TITLE";
    title.anchor = "BOTTOM_LEFT";
    title.offsetY = -0.06f;
    title.sizeSp = 34.0f;
    title.weight = "BOLD";
    TemplateSlot artist;
    artist.role = "ARTIST";
    artist.anchor = "BOTTOM_LEFT";
    artist.sizeSp = 22.0f;
    artist.weight = "REGULAR";
    return {title, artist};
}

TemplateLook readLook(const Value* source, const std::string& body) {
    TemplateLook look;
    const std::string scene = textAt(source, "sceneId");
    look.sceneId = catalogue::resolveId(blank(scene) ? catalogue::allIds().front() : scene);
    // A style this build does not have (one since removed) opens on the default style.
    if (!catalogue::knows(look.sceneId)) look.sceneId = catalogue::allIds().front();
    look.attack = std::clamp(floatAt(source, "attack", 0.6f), kAttackMin, kAttackMax);
    look.decay = std::clamp(floatAt(source, "decay", 0.12f), kDecayMin, kDecayMax);
    look.customShader = textOrNone(source, "customShader");
    const Value* params = objectAt(source, "params");
    if (params) {
        // From the text itself where it is standard JSON, as the preset reader does (viz/LookFiles).
        SceneParams read;
        std::string paletteId;
        std::string ignored;
        if (readParamsJson(body, kParamsPath, json::orgJsonText(*params).value_or(""), read, paletteId, ignored)) {
            look.params = read;
            look.customPaletteId = paletteId;
        }
        look.paramsForeign = foreignOf(params, paramsValue(look));
    }
    look.foreign = foreignOf(source, lookValue(look));
    return look;
}

TemplateLayout readLayout(const Value* source) {
    const TemplateLayout d{};
    TemplateLayout layout;
    layout.ratio = tokenAt(source, "ratio", kRatios, d.ratio.c_str());
    layout.artwork = tokenAt(source, "artwork", kArtworks, d.artwork.c_str());
    layout.artworkScale = floatAt(source, "artworkScale", d.artworkScale);
    layout.progress = tokenAt(source, "progress", kProgress, d.progress.c_str());
    layout.safeArea = floatAt(source, "safeArea", d.safeArea);
    layout.accent = argbAt(source, "accent", d.accent);
    layout.backdrop = argbAt(source, "backdrop", d.backdrop);
    layout.foreign = foreignOf(source, layoutValue(layout));
    return layout;
}

TemplateSlot readSlot(const Value& source) {
    const TemplateSlot d{};
    TemplateSlot slot;
    slot.role = tokenAt(&source, "role", kRoles, d.role.c_str());
    slot.pattern = textAt(&source, "pattern");
    slot.anchor = tokenAt(&source, "anchor", kAnchors, d.anchor.c_str());
    slot.offsetX = floatAt(&source, "offsetX", 0.0f);
    slot.offsetY = floatAt(&source, "offsetY", 0.0f);
    slot.sizeSp = floatAt(&source, "sizeSp", d.sizeSp);
    slot.color = argbAt(&source, "color", d.color);
    slot.weight = tokenAt(&source, "weight", kWeights, d.weight.c_str());
    slot.allCaps = boolAt(&source, "allCaps", false);
    slot.shadow = boolAt(&source, "shadow", true);
    slot.foreign = foreignOf(&source, slotValue(slot));
    return slot;
}

// An absent slots array is "never configured" and takes the default pair; a present empty one is no text at all.
void readText(const Value* source, VideoTemplate& t) {
    const Value* array = source ? source->get("slots") : nullptr;
    if (array && array->kind() == Value::Kind::Array) {
        for (const Value& item : array->array()) {
            if (item.kind() == Value::Kind::Object) t.slots.push_back(readSlot(item));
        }
    } else {
        t.slots = defaultSlots();
    }
    t.textForeign = foreignOf(source, textValue(t.slots, {}));
}

TemplateSegment readSegment(const Value* source) {
    TemplateSegment segment;
    if (!source) return segment;
    std::string kind = trimmed(textAt(source, "kind"));
    if (kind.empty()) kind = kWhole;
    if (caselessEqual(kind, kWhole)) return segment;
    if (caselessEqual(kind, kFixed)) {
        segment.kind = kFixed;
        segment.startMs = longAt(source, "startMs", 0);
        segment.durationMs = longAt(source, "durationMs", 0);
        return segment;
    }
    if (caselessEqual(kind, kLoudest)) {
        segment.kind = kLoudest;
        segment.durationMs = longAt(source, "durationMs", kHookMs);
        return segment;
    }
    // A kind from a later version: its name and every field it brought.
    segment.kind = kind;
    segment.fields = foreignOf(source, Object().put("kind", str(kind)).build());
    return segment;
}

TemplateExport readExport(const Value* source) {
    const TemplateExport d{};
    TemplateExport e;
    e.quality = tokenAt(source, "quality", kQualities, d.quality.c_str());
    e.fps = intAt(source, "fps", d.fps);
    e.loopSafe = boolAt(source, "loopSafe", true);
    e.audio = tokenAt(source, "audio", kAudio, d.audio.c_str());
    e.segment = readSegment(objectAt(source, "segment"));
    const std::string pattern = textAt(source, "fileNamePattern");
    e.fileNamePattern = blank(pattern) ? d.fileNamePattern : pattern;
    e.foreign = foreignOf(source, exportValue(e));
    return e;
}

// UTF-16 units of the code point that starts with lead byte `b`.
size_t utf16Units(unsigned char b) { return (b >> 3) == 0x1E ? 2 : 1; }

size_t utf8Length(unsigned char b) { return b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1; }

}  // namespace

VideoTemplate defaultTemplate() {
    VideoTemplate t;
    t.look.sceneId = catalogue::allIds().front();
    t.slots = defaultSlots();
    return t;
}

std::string newTemplateId() {
    static thread_local std::mt19937_64 engine{[] {
        std::random_device seed;
        return (static_cast<uint64_t>(seed()) << 32) ^ static_cast<uint64_t>(seed());
    }()};
    uint64_t hi = engine();
    uint64_t lo = engine();
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // IETF variant
    char out[64];
    std::snprintf(out, sizeof out, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xFFFF), static_cast<unsigned>(hi & 0xFFFF),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
    return out;
}

TemplateRead readTemplate(std::string_view text) {
    TemplateRead out;
    const size_t start = text.find('{');
    if (start == std::string_view::npos) {
        out.why = "the template was empty";
        return out;
    }
    std::string body(text.substr(start));
    for (size_t i = body.size(); i > 0;) {
        size_t lead = i - 1;
        while (lead > 0 && (static_cast<unsigned char>(body[lead]) & 0xC0) == 0x80) --lead;
        if (whitespaceAt(body, lead) != i - lead) break;
        body.resize(lead);
        i = lead;
    }
    const std::optional<Value> root = json::parseLeading(body);
    if (!root || root->kind() != Value::Kind::Object) {
        out.why = "this is not JSON";
        return out;
    }
    if (!caselessEqual(textAt(&*root, "format"), kTemplateFormat)) {
        out.status = TemplateStatus::NotATemplate;
        out.why = std::string("no 'format': '") + kTemplateFormat + "' marker";
        return out;
    }
    VideoTemplate& t = out.value;
    t.id = trimmed(textAt(&*root, "id"));
    if (t.id.empty()) t.id = newTemplateId();
    t.name = trimmed(textAt(&*root, "name"));
    if (t.name.empty()) t.name = kUntitled;
    t.look = readLook(objectAt(&*root, "look"), body);
    t.layout = readLayout(objectAt(&*root, "layout"));
    readText(objectAt(&*root, "text"), t);
    t.exportSettings = readExport(objectAt(&*root, "export"));
    t.author = textAt(&*root, "author");
    t.notes = textAt(&*root, "notes");
    t.createdAtMs = longAt(&*root, "createdAtMs", 0);
    t.formatVersion = intAt(&*root, "formatVersion", kTemplateFormatVersion);
    t.foreign = foreignOf(&*root, templateValue(t));
    out.status = TemplateStatus::Parsed;
    return out;
}

std::string writeTemplate(const VideoTemplate& t) { return json::orgJsonIndented(templateValue(t), 2).value_or("{}"); }

std::optional<std::string> templateLinkIn(std::string_view text) {
    size_t at = std::string_view::npos;
    for (size_t i = 0; i + kTemplatePrefix.size() <= text.size(); ++i) {
        if (caselessEqual(text.substr(i, kTemplatePrefix.size()), kTemplatePrefix)) {
            at = i;
            break;
        }
    }
    if (at == std::string_view::npos) return std::nullopt;
    size_t end = at + kTemplatePrefix.size();
    while (end < text.size() && whitespaceAt(text, end) == 0) end += utf8Length(static_cast<unsigned char>(text[end]));
    end = std::min(end, text.size());
    return std::string(kPresetPrefix) + std::string(text.substr(at + kTemplatePrefix.size(), end - at - kTemplatePrefix.size()));
}

std::optional<std::string> templateLinkOf(std::string_view presetLink, size_t maxLength) {
    if (presetLink.substr(0, kPresetPrefix.size()) != kPresetPrefix) return std::nullopt;
    std::string link = std::string(kTemplatePrefix) + std::string(presetLink.substr(kPresetPrefix.size()));
    if (link.size() > maxLength) return std::nullopt;
    return link;
}

std::string templateShareName(std::string_view name) {
    std::string safe;
    for (const char c : name) {
        const bool unsafe = c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        safe += unsafe ? '_' : c;
    }
    const std::string stem = trimmed(safe);
    size_t units = 0;
    size_t cut = 0;
    while (cut < stem.size()) {
        const auto lead = static_cast<unsigned char>(stem[cut]);
        if (units + utf16Units(lead) > 60) break;
        units += utf16Units(lead);
        cut += std::min(utf8Length(lead), stem.size() - cut);
    }
    std::string out = trimmed(std::string_view(stem).substr(0, cut));
    if (out.empty()) out = kUntitled;
    return out + ".geode.json";
}

}  // namespace geode::viz::files
