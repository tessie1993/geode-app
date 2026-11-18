// The backup rules (see Backup.hpp). Ported from data/backup/BackupFormat.kt, BackupMigrations.kt, BackupArchive.kt,
// BackupManifest.kt and the decisions of BackupRepository.kt. Where a Kotlin line leaned on the JVM it is named in a
// comment and matched here: String.length counts UTF-16 units, String.compareTo orders by UTF-16 units, org.json's
// reading of numbers and strings comes from util/Json.
#include "backup/Backup.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <set>

#include "util/Json.hpp"

namespace geode::backup {

namespace {

namespace js = geode::json;

Failure fail(Problem p, std::string message) { return Failure{p, std::move(message)}; }

// ---- the table (BackupFormat.kt BackupSection, deviceLocalKeys, excludedSubtrees) -------------------------------

// Preference file names (GeodePrefsFiles.kt).
constexpr const char* kGeneral = "geode-prefs";
constexpr const char* kViz = "geode-viz";
constexpr const char* kPlayer = "geode-player";
constexpr const char* kFavourites = "geode-favourites";
constexpr const char* kAudioFx = "geode-audiofx";
constexpr const char* kModulation = "geode-mod";
constexpr const char* kLibrary = "geode-library";
constexpr const char* kBackground = "geode-background";
constexpr const char* kWallpaper = "geode-wallpaper";

constexpr const char* kPrefsDir = "prefs/";
constexpr const char* kPrefsSuffix = ".json";

struct Section {
    std::string id;
    std::vector<std::string> prefsFiles;
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    // Keys this section owns inside a prefs file another section owns, by prefs file name.
    std::vector<std::pair<std::string, std::set<std::string>>> borrowed;
};

const std::vector<Section>& sectionTable() {
    static const std::vector<Section> table = {
        {"settings",
         {kGeneral, kViz, kPlayer, kAudioFx, kLibrary, kBackground, kWallpaper},
         {},
         {},
         {}},
        {"creative",
         {kModulation},
         {"presets", "palettes", "templates", "editor", "projects", "project-takes", "project-templates"},
         {},
         // MacroStore.KEYS + LfoStore.KEYS + PaletteStackStore.KEYS + LookStore.LAYER_KEYS
         {{kGeneral,
           {"macro_bank", "lfos", "adsrs", "reactions", "reactions_hand", "reactions_unread", "palette_stack", "customize_layer"}}}},
        {"music", {kFavourites}, {"music-playlists", "smart-playlists"}, {"library.json", "history.json"}, {}},
        {"milk", {}, {"milk"}, {}, {}},
    };
    return table;
}

const Section* sectionById(const std::string& id) {
    for (const auto& s : sectionTable()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const std::set<std::string>* deviceLocal(const std::string& file) {
    // PerformanceStore.KEYS, MacroStore.RETIRED_KEYS and CustomizeGroupsCodec.KEYS are folded into geode-prefs.
    static const std::map<std::string, std::set<std::string>> table = {
        {kGeneral,
         {"gui_safety_acknowledged", "gui_setup_done", "gui_tutorial_seen", "gui_tutorial_done", "gui_tutorial_resume",
          "gui_tutorial_tips", "gui_tutorial_off", "preset_mirror_uri", "perf_render_scale", "perf_fps_cap",
          "perf_march_scale", "perf_post_fx", "perf_adapt_frame_time", "perf_adaptive", "perf_liquid_hot", "macro_knobs", "macro_locks",
          "macro_presets", "customize_open_groups", "customize_tab"}},
        {kLibrary, {"roots"}},
        {kPlayer, {"picture_delay_ms"}},
        {kBackground, {"background_uri"}},
        {kWallpaper, {"mix_refused"}},
    };
    const auto it = table.find(file);
    return it == table.end() ? nullptr : &it->second;
}

// Caches: regenerated textures and the links MilkTextureLinks rebuilds. Never exported; a restore keeps the phone's.
const std::vector<std::string>& excludedSubtrees() {
    static const std::vector<std::string> v = {"milk/generated", "milk/textures/.links"};
    return v;
}

// AtomicWrite's in-flight and quarantined files.
const std::vector<std::string>& excludedSuffixes() {
    static const std::vector<std::string> v = {".tmp", ".corrupt"};
    return v;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::string prefsPath(const std::string& file) { return std::string(kPrefsDir) + file + kPrefsSuffix; }
std::string slicePath(const Section& s, const std::string& file) {
    return std::string(kPrefsDir) + s.id + "/" + file + kPrefsSuffix;
}

std::vector<PrefsEntryInfo> prefsEntriesOf(const Section& s) {
    std::vector<PrefsEntryInfo> out;
    for (const auto& f : s.prefsFiles) out.push_back({f, prefsPath(f)});
    for (const auto& b : s.borrowed) out.push_back({b.first, slicePath(s, b.first)});
    return out;
}

// ---- UTF-16 views of UTF-8 text (String.length, String[i], String.compareTo) -------------------------------------

// A permissive UTF-8 reader: enough to count and order what a decoded java String would. Not a validator.
std::vector<char16_t> toUtf16(const std::string& s) {
    std::vector<char16_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        size_t len = b < 0x80 ? 1 : b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        if (i + len > s.size()) len = s.size() - i;
        uint32_t cp = len == 1 ? b : (len == 2 ? b & 0x1Fu : len == 3 ? b & 0x0Fu : b & 0x07u);
        for (size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += len;
    }
    return out;
}

bool javaLess(const std::string& a, const std::string& b) {
    const auto ua = toUtf16(a);
    const auto ub = toUtf16(b);
    return std::lexicographical_compare(ua.begin(), ua.end(), ub.begin(), ub.end());
}

std::string hexOf(const std::string& raw) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const char c : raw) {
        const auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// Double.toFloat(): round to nearest, overflow to infinity (static_cast would be undefined out of range).
double javaFloat(double d) {
    if (std::isnan(d)) return d;
    const double overflow = std::ldexp(1.0, 128) - std::ldexp(1.0, 103);
    if (std::fabs(d) >= overflow) return d < 0 ? -HUGE_VAL : HUGE_VAL;
    if (std::fabs(d) > static_cast<double>(FLT_MAX)) return d < 0 ? -static_cast<double>(FLT_MAX) : static_cast<double>(FLT_MAX);
    return static_cast<double>(static_cast<float>(d));
}

// ---- org.json getters (Android JSONObject.getString/getInt/..., which coerce like util/Json) ----------------------

const js::Value* member(const js::Value& obj, const std::string& key) { return obj.get(key); }

bool getString(const js::Value& obj, const std::string& key, std::string& out) {
    const js::Value* v = member(obj, key);
    if (!v) return false;
    const auto s = js::asString(*v);
    if (!s) return false;
    out = *s;
    return true;
}
bool getInt(const js::Value& obj, const std::string& key, int& out) {
    const js::Value* v = member(obj, key);
    const auto r = v ? js::asInt(*v) : std::nullopt;
    if (!r) return false;
    out = *r;
    return true;
}
bool getLong(const js::Value& obj, const std::string& key, int64_t& out) {
    const js::Value* v = member(obj, key);
    const auto r = v ? js::asLong(*v) : std::nullopt;
    if (!r) return false;
    out = *r;
    return true;
}
const js::Value* getArray(const js::Value& obj, const std::string& key) {
    const js::Value* v = member(obj, key);
    return (v && v->kind() == js::Value::Kind::Array) ? v : nullptr;
}
const js::Value* getObject(const js::Value& obj, const std::string& key) {
    const js::Value* v = member(obj, key);
    return (v && v->kind() == js::Value::Kind::Object) ? v : nullptr;
}

js::Value wholeValue(int64_t n) { return js::Value::whole(static_cast<double>(n), n); }

js::Value objectOf(std::vector<std::pair<std::string, js::Value>> members) {
    std::map<std::string, js::Value> map;
    std::vector<std::string> keys;
    for (auto& m : members) {
        keys.push_back(m.first);
        map[m.first] = std::move(m.second);
    }
    return js::Value::object(std::move(map), std::move(keys));
}

}  // namespace

// ---- layout and single rules ------------------------------------------------------------------------------------

std::vector<SectionInfo> layout() {
    std::vector<SectionInfo> out;
    for (const auto& s : sectionTable()) {
        SectionInfo info;
        info.id = s.id;
        info.prefsEntries = prefsEntriesOf(s);
        info.dirs = s.dirs;
        info.files = s.files;
        info.roots = s.dirs;
        info.roots.insert(info.roots.end(), s.files.begin(), s.files.end());
        out.push_back(std::move(info));
    }
    return out;
}

std::string defaultFileName(int year, int month, int day) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "beatrealms-backup-%04d%02d%02d.zip", year, month, day);
    return buf;
}

bool isSafePath(const std::string& path) {
    if (path.empty()) return false;
    if (toUtf16(path).size() > static_cast<size_t>(kMaxPathLength)) return false;  // String.length
    if (path[0] == '/' || path.find('\\') != std::string::npos || path.find('\0') != std::string::npos) return false;
    // path[1] == ':' on a String is its second UTF-16 unit; ':' is ASCII, so it is the byte after a BMP first char.
    const auto first = static_cast<unsigned char>(path[0]);
    const size_t firstLen = first < 0xC0 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
    if (first < 0xF0 && path.size() > firstLen && path[firstLen] == ':') return false;
    size_t start = 0;
    while (true) {
        const size_t slash = path.find('/', start);
        const std::string seg = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

bool isPrefsPath(const std::string& path) { return startsWith(path, kPrefsDir) && endsWith(path, kPrefsSuffix); }

std::string sectionOfPath(const std::string& path) {
    if (!isSafePath(path)) return "";
    for (const auto& suffix : excludedSuffixes()) {
        if (endsWith(path, suffix)) return "";
    }
    for (const auto& sub : excludedSubtrees()) {
        if (path == sub || startsWith(path, sub + "/")) return "";
    }
    if (startsWith(path, kPrefsDir)) {
        if (!endsWith(path, kPrefsSuffix)) return "";
        const std::string tail = path.substr(std::string(kPrefsDir).size());
        if (tail.size() < std::string(kPrefsSuffix).size()) return "";
        const std::string name = tail.substr(0, tail.size() - std::string(kPrefsSuffix).size());
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            const size_t slash = name.find('/', start);
            parts.push_back(name.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        if (parts.size() == 1) {
            for (const auto& s : sectionTable()) {
                if (contains(s.prefsFiles, name)) return s.id;
            }
            return "";
        }
        if (parts.size() == 2) {
            const Section* s = sectionById(parts[0]);
            if (!s) return "";
            for (const auto& b : s->borrowed) {
                if (b.first == parts[1]) return s->id;
            }
        }
        return "";
    }
    for (const auto& s : sectionTable()) {
        if (contains(s.files, path)) return s.id;
        for (const auto& d : s.dirs) {
            if (startsWith(path, d + "/")) return s.id;
        }
    }
    return "";
}

std::string sectionOfKey(const std::string& prefsFile, const std::string& key) {
    if (const auto* local = deviceLocal(prefsFile)) {
        if (local->count(key)) return "";
    }
    for (const auto& s : sectionTable()) {
        for (const auto& b : s.borrowed) {
            if (b.first == prefsFile && b.second.count(key)) return s.id;
        }
    }
    for (const auto& s : sectionTable()) {
        if (contains(s.prefsFiles, prefsFile)) return s.id;
    }
    return "";
}

bool isPortableKey(const std::string& prefsFile, const std::string& key) {
    const auto* local = deviceLocal(prefsFile);
    return !(local && local->count(key));
}

bool isInsideStaging(const std::string& root, const std::string& target, char separator) {
    return startsWith(target, root + separator);
}

int64_t remainingBudget(int64_t used, const Limits& limits) { return limits.maxTotalBytes - used; }

// ---- preferences as JSON ----------------------------------------------------------------------------------------

Failure encodePrefs(const std::vector<PrefEntry>& values, std::string& out) {
    std::vector<const PrefEntry*> sorted;
    for (const auto& e : values) {
        if (e.value.type != PrefType::Unsupported) sorted.push_back(&e);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const PrefEntry* a, const PrefEntry* b) { return javaLess(a->key, b->key); });  // toSortedMap
    std::vector<std::pair<std::string, js::Value>> root;
    for (const PrefEntry* e : sorted) {
        const PrefValue& v = e->value;
        std::string type;
        js::Value value;
        switch (v.type) {
            case PrefType::String: type = "string"; value = js::Value::string(v.text); break;
            case PrefType::Int: type = "int"; value = wholeValue(v.integer); break;
            case PrefType::Long: type = "long"; value = wholeValue(v.integer); break;
            case PrefType::Float:
                if (!std::isfinite(v.real)) return fail(Problem::IoFailed, "non-finite float for " + e->key);
                type = "float";
                value = js::Value::number(v.real);
                break;
            case PrefType::Boolean: type = "boolean"; value = js::Value::boolean(v.flag); break;
            case PrefType::StringSet: {
                type = "stringSet";
                std::vector<std::string> items = v.items;
                std::sort(items.begin(), items.end(), javaLess);
                items.erase(std::unique(items.begin(), items.end()), items.end());
                std::vector<js::Value> array;
                for (auto& s : items) array.push_back(js::Value::string(std::move(s)));
                value = js::Value::array(std::move(array));
                break;
            }
            case PrefType::Unsupported: continue;
        }
        root.emplace_back(e->key, objectOf({{"type", js::Value::string(type)}, {"value", std::move(value)}}));
    }
    const auto text = js::orgJsonIndented(objectOf(std::move(root)), 2);
    if (!text) return fail(Problem::IoFailed, "prefs not encodable");
    out = *text;
    return {};
}

Failure decodePrefs(const std::string& text, std::vector<PrefEntry>& out) {
    out.clear();
    const auto root = js::parseLeading(text);
    if (!root || root->kind() != js::Value::Kind::Object) return fail(Problem::Damaged, "prefs file is not valid JSON");
    for (const std::string& key : root->keys()) {
        const js::Value* typed = getObject(*root, key);
        std::string type;
        if (!typed || !getString(*typed, "type", type)) return fail(Problem::Damaged, "prefs file is not valid JSON");
        PrefEntry entry;
        entry.key = key;
        PrefValue& v = entry.value;
        const js::Value* raw = typed->get("value");
        bool ok = raw != nullptr;
        if (ok && type == "string") {
            v.type = PrefType::String;
            ok = getString(*typed, "value", v.text);
        } else if (ok && type == "int") {
            v.type = PrefType::Int;
            int n = 0;
            ok = getInt(*typed, "value", n);
            v.integer = n;
        } else if (ok && type == "long") {
            v.type = PrefType::Long;
            ok = getLong(*typed, "value", v.integer);
        } else if (ok && type == "float") {
            v.type = PrefType::Float;
            const auto d = js::asDouble(*raw);
            ok = d.has_value();
            if (ok) v.real = javaFloat(*d);
        } else if (ok && type == "boolean") {
            v.type = PrefType::Boolean;
            const auto b = js::asBoolean(*raw);
            ok = b.has_value();
            if (ok) v.flag = *b;
        } else if (ok && type == "stringSet") {
            v.type = PrefType::StringSet;
            const js::Value* array = getArray(*typed, "value");
            ok = array != nullptr;
            if (ok) {
                for (const auto& item : array->array()) {
                    const auto s = js::asString(item);
                    if (!s) {
                        ok = false;
                        break;
                    }
                    v.items.push_back(*s);
                }
                std::sort(v.items.begin(), v.items.end(), javaLess);
                v.items.erase(std::unique(v.items.begin(), v.items.end()), v.items.end());
            }
        } else if (ok) {
            return fail(Problem::Damaged, "unknown prefs value type " + type + " for " + key);
        }
        if (!ok) return fail(Problem::Damaged, "prefs file is not valid JSON");
        out.push_back(std::move(entry));
    }
    return {};
}

// ---- the manifest -----------------------------------------------------------------------------------------------

std::string encodeManifest(const Manifest& m) {
    std::vector<js::Value> sections;
    for (const auto& s : m.sections) sections.push_back(js::Value::string(s));
    std::vector<js::Value> entries;
    for (const auto& e : m.entries) {
        entries.push_back(objectOf(
            {{"path", js::Value::string(e.path)}, {"size", wholeValue(e.size)}, {"sha256", js::Value::string(e.sha256)}}));
    }
    const js::Value root = objectOf({{"format", js::Value::string(m.format)},
                                     {"version", wholeValue(m.version)},
                                     {"appVersionCode", wholeValue(m.appVersionCode)},
                                     {"createdAtMs", wholeValue(m.createdAtMs)},
                                     {"sections", js::Value::array(std::move(sections))},
                                     {"entries", js::Value::array(std::move(entries))}});
    return js::orgJsonIndented(root, 2).value_or("{}");
}

Failure decodeManifest(const std::string& text, Manifest& out) {
    const auto root = js::parseLeading(text);
    if (!root || root->kind() != js::Value::Kind::Object) return fail(Problem::NotABackup, "manifest is not JSON");
    if (root->optString("format") != kFormatId) {
        return fail(Problem::NotABackup, "format is '" + root->optString("format") + "'");
    }
    int version = 0;
    if (const js::Value* v = root->get("version")) version = js::asInt(*v).value_or(0);  // optInt(.., 0)
    if (version > kCurrentVersion) {
        return fail(Problem::NewerVersion,
                    "version " + std::to_string(version) + ", this app reads up to " + std::to_string(kCurrentVersion));
    }
    if (version < 1) return fail(Problem::Damaged, "version " + std::to_string(version));
    const Failure incomplete = fail(Problem::Damaged, "manifest is incomplete");
    const js::Value* sections = getArray(*root, "sections");
    const js::Value* entries = getArray(*root, "entries");
    if (!sections || !entries) return incomplete;
    Manifest m;
    m.format = kFormatId;
    m.version = version;
    if (!getInt(*root, "appVersionCode", m.appVersionCode)) return incomplete;
    if (!getLong(*root, "createdAtMs", m.createdAtMs)) return incomplete;
    for (const auto& s : sections->array()) {
        const auto str = js::asString(s);
        if (!str) return incomplete;
        m.sections.push_back(*str);
    }
    for (const auto& e : entries->array()) {
        if (e.kind() != js::Value::Kind::Object) return incomplete;
        EntryRecord r;
        if (!getString(e, "path", r.path) || !getLong(e, "size", r.size) || !getString(e, "sha256", r.sha256)) return incomplete;
        m.entries.push_back(std::move(r));
    }
    out = std::move(m);
    return {};
}

// ---- migrations -------------------------------------------------------------------------------------------------

Failure migrationPath(int version, int target, const std::vector<int>& stepFroms, std::vector<int>& path) {
    path.clear();
    while (version < target) {
        if (std::find(stepFroms.begin(), stepFroms.end(), version) == stepFroms.end()) {
            return fail(Problem::Damaged, "no migration from version " + std::to_string(version));
        }
        path.push_back(version);
        ++version;
    }
    return {};
}

namespace {

// Version 1 -> 2 (BackupMigrations.splitBorrowedKeys): the macros, LFOs, envelopes and reactions that lived inside
// Settings' prefs file move into Creative's slice, only when the backup holds Creative already.
Failure splitBorrowedKeys(Manifest& m, std::map<std::string, std::string>& texts, std::vector<FileWrite>& writes) {
    for (const auto& section : sectionTable()) {
        if (!contains(m.sections, section.id)) continue;
        for (const auto& borrowed : section.borrowed) {
            const std::string& name = borrowed.first;
            const std::string ownerPath = prefsPath(name);
            const bool listed = std::any_of(m.entries.begin(), m.entries.end(),
                                            [&](const EntryRecord& e) { return e.path == ownerPath; });
            const auto text = texts.find(ownerPath);
            if (!listed || text == texts.end()) continue;  // no owner file: nothing to move
            std::vector<PrefEntry> all;
            if (Failure f = decodePrefs(text->second, all); !f.ok()) return f;
            std::vector<PrefEntry> moved;
            std::vector<PrefEntry> kept;
            for (auto& e : all) (borrowed.second.count(e.key) ? moved : kept).push_back(std::move(e));
            if (moved.empty()) continue;
            const std::string slice = slicePath(section, name);
            std::vector<EntryRecord> entries;
            for (const auto& e : m.entries) {
                if (e.path != ownerPath && e.path != slice) entries.push_back(e);
            }
            for (const auto& [path, values] : {std::pair<std::string, const std::vector<PrefEntry>*>{ownerPath, &kept},
                                                std::pair<std::string, const std::vector<PrefEntry>*>{slice, &moved}}) {
                std::string bytes;
                if (Failure f = encodePrefs(*values, bytes); !f.ok()) return f;
                // The rewritten file's SHA-256 is the platform's to compute; nothing reads it after unpack.
                entries.push_back({path, static_cast<int64_t>(bytes.size()), ""});
                texts[path] = bytes;
                const auto at = std::find_if(writes.begin(), writes.end(), [&](const FileWrite& w) { return w.path == path; });
                if (at != writes.end()) at->bytes = bytes;
                else writes.push_back({path, bytes});
            }
            m.entries = std::move(entries);
        }
    }
    return {};
}

}  // namespace

// ---- export -----------------------------------------------------------------------------------------------------

ExportPlan planExport(const ExportRequest& request, const Limits& limits) {
    ExportPlan plan;
    for (const auto& section : sectionTable()) {
        for (const auto& info : prefsEntriesOf(section)) {
            std::vector<PrefEntry> mine;
            for (const auto& snap : request.prefs) {
                if (snap.file != info.file) continue;
                for (const auto& e : snap.entries) {
                    if (sectionOfKey(info.file, e.key) == section.id) mine.push_back(e);
                }
            }
            ExportSource src;
            src.path = info.path;
            src.inlineBytes = true;
            if (Failure f = encodePrefs(mine, src.bytes); !f.ok()) {
                plan.failure = f;
                plan.sources.clear();
                return plan;
            }
            plan.sources.push_back(std::move(src));
        }
        for (const auto& found : request.files) {
            if (found.section != section.id || sectionOfPath(found.path) != section.id) continue;
            ExportSource src;
            src.path = found.path;
            plan.sources.push_back(std::move(src));
        }
    }
    if (plan.sources.size() > static_cast<size_t>(limits.maxEntries)) {
        plan.failure = fail(Problem::TooLarge, std::to_string(plan.sources.size()) + " files");
        plan.sources.clear();
    }
    return plan;
}

ExportResult finishExport(const std::vector<ExportedEntry>& entries, int appVersionCode, int64_t createdAtMs,
                          const Limits& limits) {
    ExportResult r;
    Manifest m;
    m.format = kFormatId;
    m.version = kCurrentVersion;
    m.appVersionCode = appVersionCode;
    m.createdAtMs = createdAtMs;
    int64_t total = 0;
    for (const auto& e : entries) {
        if (e.size < 0 || e.digest.size() != 32) {
            r.failure = fail(Problem::IoFailed, "bad entry " + e.path);
            return r;
        }
        if (e.size > limits.maxTotalBytes - total) {
            r.failure = fail(Problem::TooLarge, "over the size cap");
            return r;
        }
        total += e.size;
        m.entries.push_back({e.path, e.size, hexOf(e.digest)});
    }
    for (const auto& s : sectionTable()) m.sections.push_back(s.id);
    r.manifestText = encodeManifest(m);
    return r;
}

// ---- unpack -----------------------------------------------------------------------------------------------------

UnpackPlan planUnpack(const UnpackRequest& request, const Limits& limits) {
    UnpackPlan plan;
    if (request.entryCount > static_cast<int64_t>(limits.maxEntries) + 1) {
        plan.failure = fail(Problem::TooLarge, std::to_string(request.entryCount) + " entries");
        return plan;
    }
    if (!request.hasManifest) {
        plan.failure = fail(Problem::NotABackup, "no manifest.json");
        return plan;
    }
    if (request.manifestOverCap) {
        plan.failure = fail(Problem::TooLarge, "over the size cap");
        return plan;
    }
    Manifest manifest;
    if (Failure f = decodeManifest(request.manifestText, manifest); !f.ok()) {
        plan.failure = f;
        return plan;
    }
    std::map<std::string, const EntryRecord*> expected;
    for (const auto& e : manifest.entries) expected[e.path] = &e;
    if (expected.size() != manifest.entries.size()) {
        plan.failure = fail(Problem::Damaged, "manifest lists a path twice");
        return plan;
    }
    std::set<std::string> seen;
    int64_t promisedSoFar = 0;
    for (size_t i = 0; i < request.entries.size(); ++i) {
        const ZipEntryInfo& entry = request.entries[i];
        // Directories are implied by the files; nothing is created for them.
        if (entry.isDirectory || entry.name == kManifestPath) continue;
        if (!isSafePath(entry.name)) {
            plan.afterExtract = fail(Problem::UnsafePath, "unsafe path '" + entry.name + "'");
            break;
        }
        if (!seen.insert(entry.name).second) {
            plan.afterExtract = fail(Problem::Damaged, "'" + entry.name + "' appears twice");
            break;
        }
        const auto promised = expected.find(entry.name);
        if (promised == expected.end()) {
            plan.afterExtract = fail(Problem::Damaged, "'" + entry.name + "' is not in the manifest");
            break;
        }
        ExtractItem item;
        item.entryIndex = i;
        item.path = entry.name;
        item.promisedSize = promised->second->size;
        item.promisedSha = promised->second->sha256;
        // Every earlier item matched its promise or the loop stopped there, so what has passed is their promised sizes.
        item.budget = limits.maxTotalBytes - promisedSoFar;
        plan.extract.push_back(item);
        if (item.promisedSize > 0) promisedSoFar = std::min<int64_t>(promisedSoFar + std::min(item.promisedSize, limits.maxTotalBytes + 1), limits.maxTotalBytes + 1);
    }
    if (plan.afterExtract.ok() && seen.size() != expected.size()) {
        plan.afterExtract = fail(Problem::Damaged, std::to_string(expected.size() - seen.size()) + " files are missing");
    }
    std::set<std::string> read;
    for (const auto& e : manifest.entries) {
        if (isPrefsPath(e.path) && !sectionOfPath(e.path).empty() && read.insert(e.path).second) {
            plan.readForFinish.push_back(e.path);
        }
    }
    return plan;
}

Failure checkExtracted(const ExtractItem& item, int64_t size, const std::string& digest, bool overBudget) {
    if (overBudget || size > item.budget) return fail(Problem::TooLarge, "over the size cap");
    if (size != item.promisedSize || digest.size() != 32 || hexOf(digest) != item.promisedSha) {
        return fail(Problem::Damaged, "'" + item.path + "' does not match its checksum");
    }
    return {};
}

FinishResult finishUnpack(const std::string& manifestText, const std::map<std::string, std::string>& staged) {
    FinishResult r;
    Manifest m;
    if (Failure f = decodeManifest(manifestText, m); !f.ok()) {
        r.failure = f;
        return r;
    }
    std::map<std::string, std::string> texts = staged;
    // The migration chain; each step turns its version into the next.
    std::vector<int> path;
    if (Failure f = migrationPath(m.version, kCurrentVersion, {1}, path); !f.ok()) {
        r.failure = f;
        return r;
    }
    for (const int from : path) {
        if (from == 1) {
            if (Failure f = splitBorrowedKeys(m, texts, r.writes); !f.ok()) {
                r.failure = f;
                r.writes.clear();
                return r;
            }
        }
        m.version = from + 1;
    }
    // The allowlist runs after migration: it is the current version's.
    std::set<std::string> known;
    for (const auto& id : m.sections) {
        if (!sectionById(id)) {
            r.failure = fail(Problem::Damaged, "unknown sections [" + id + "]");
            r.writes.clear();
            return r;
        }
        known.insert(id);
    }
    for (const auto& e : m.entries) {
        const std::string section = sectionOfPath(e.path);
        if (section.empty()) {
            r.failure = fail(Problem::UnsafePath, "'" + e.path + "' is outside what a backup may restore");
            r.writes.clear();
            return r;
        }
        if (!known.count(section)) {
            r.failure = fail(Problem::Damaged, "'" + e.path + "' belongs to a section not listed");
            r.writes.clear();
            return r;
        }
        if (isPrefsPath(e.path)) {
            const auto text = texts.find(e.path);
            std::vector<PrefEntry> ignored;
            Failure f = text == texts.end() ? fail(Problem::IoFailed, "no staged text for " + e.path)
                                            : decodePrefs(text->second, ignored);
            if (!f.ok()) {
                r.failure = f;
                r.writes.clear();
                return r;
            }
        }
    }
    for (const auto& s : sectionTable()) {
        if (known.count(s.id)) r.sections.push_back(s.id);
    }
    r.createdAtMs = m.createdAtMs;
    r.appVersionCode = m.appVersionCode;
    r.manifest = std::move(m);
    return r;
}

// ---- restore ----------------------------------------------------------------------------------------------------

RestorePlan planRestore(const RestoreRequest& request) {
    RestorePlan plan;
    if (!request.hasStaged) {
        plan.failure = fail(Problem::IoFailed, "nothing staged");
        return plan;
    }
    for (const auto& section : sectionTable()) {
        if (!contains(request.requested, section.id) || !contains(request.stagedSections, section.id)) continue;
        SectionRestore sr;
        sr.id = section.id;
        sr.targets = section.dirs;
        sr.targets.insert(sr.targets.end(), section.files.begin(), section.files.end());
        for (const auto& subtree : excludedSubtrees()) {
            if (std::any_of(sr.targets.begin(), sr.targets.end(), [&](const std::string& t) { return startsWith(subtree, t + "/"); })) {
                sr.carryOver.push_back(subtree);
            }
        }
        sr.ensureDirs = section.dirs;
        for (const auto& info : prefsEntriesOf(section)) {
            const auto text = request.stagedPrefs.find(info.path);
            if (text == request.stagedPrefs.end()) continue;  // no staged file: the phone's keys stay as they are
            std::vector<PrefEntry> values;
            if (Failure f = decodePrefs(text->second, values); !f.ok()) {
                plan.failure = f;
                plan.sections.clear();
                return plan;
            }
            PrefsRestore pr;
            pr.file = info.file;
            pr.stagedPath = info.path;
            // Device-local keys and other sections' keys are left as they are, and any in the backup are ignored.
            const auto current = request.currentKeys.find(info.file);
            if (current != request.currentKeys.end()) {
                for (const auto& key : current->second) {
                    if (sectionOfKey(info.file, key) == section.id) pr.removeKeys.push_back(key);
                }
            }
            for (auto& v : values) {
                if (sectionOfKey(info.file, v.key) == section.id) pr.put.push_back(std::move(v));
            }
            sr.prefs.push_back(std::move(pr));
        }
        plan.sections.push_back(std::move(sr));
    }
    return plan;
}

}  // namespace geode::backup
