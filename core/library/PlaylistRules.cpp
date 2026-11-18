#include "library/PlaylistRules.hpp"

#include <algorithm>
#include <set>

#include "library/JsonRead.hpp"
#include "library/JvmText.hpp"
#include "util/Json.hpp"

namespace geode::library {

namespace {

using jsonread::getString;

// File.extension / nameWithoutExtension: after and before the last '.'.
std::string extensionOf(const std::string& file) {
    const size_t dot = file.rfind('.');
    return dot == std::string::npos ? "" : file.substr(dot + 1);
}

std::string withoutExtension(const std::string& file) {
    const size_t dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

// list(): the playlists in the folder, sorted.
std::vector<MusicPlaylist> listed(const std::vector<std::string>& texts) { return playlistList(texts); }

const MusicPlaylist* named(const std::vector<MusicPlaylist>& lists, const std::string& name) {
    for (const auto& p : lists) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

PlaylistOutcome saved(MusicPlaylist p) {
    PlaylistOutcome out;
    out.json = playlistToJson(p);
    out.write = true;
    out.playlist = std::move(p);
    return out;
}

PlaylistOutcome unchanged(MusicPlaylist p) {
    PlaylistOutcome out;
    out.playlist = std::move(p);
    return out;
}

}  // namespace

std::optional<MusicPlaylist> playlistParse(const std::string& text) {
    const auto root = jsonread::objectOf(text);
    if (!root) return std::nullopt;
    MusicPlaylist p;
    const auto name = getString(*root, "name");
    if (!name) return std::nullopt;
    p.name = *name;
    const json::Value* arr = root->get("tracks");
    if (!arr || arr->kind() != json::Value::Kind::Array) return std::nullopt;
    for (const json::Value& item : arr->array()) {
        const auto uri = jsonread::stringOf(item);
        if (!uri) return std::nullopt;
        p.tracks.push_back(*uri);
    }
    return p;
}

std::string playlistToJson(const MusicPlaylist& p) {
    std::string out = "{\"name\":" + json::quoted(p.name) + ",\"tracks\":[";
    for (size_t i = 0; i < p.tracks.size(); ++i) {
        if (i) out += ',';
        out += json::quoted(p.tracks[i]);
    }
    return out + "]}";
}

std::vector<MusicPlaylist> playlistList(const std::vector<std::string>& fileTexts) {
    std::vector<MusicPlaylist> out;
    for (const std::string& text : fileTexts) {
        if (auto p = playlistParse(text)) out.push_back(std::move(*p));
    }
    jvm::stableSortByLowercase(out, [](const MusicPlaylist& p) -> const std::string& { return p.name; });
    return out;
}

PlaylistOutcome playlistApply(const PlaylistFolder& folder, const PlaylistEdit& edit, const StemFn& stem) {
    const auto takenNames = [&] {
        std::vector<std::string> taken;
        for (const auto& p : listed(folder.fileTexts)) taken.push_back(p.name);
        return taken;
    };
    switch (edit.kind) {
        case PlaylistEditKind::Create: {
            // MusicLibraryRepository.createMusicPlaylist: a blank name does nothing; a taken one gets a free name.
            if (jvm::isBlank(edit.name)) return unchanged({edit.name, {}});
            MusicPlaylist p{playlistUniqueName(takenNames(), jvm::trim(edit.name), stem), {}};
            std::set<std::string> seen;
            for (const std::string& t : edit.tracks) {
                if (seen.insert(t).second) p.tracks.push_back(t);  // List.distinct(): first of each, in order
            }
            return saved(std::move(p));
        }
        case PlaylistEditKind::Import:
            return saved({playlistUniqueName(takenNames(), edit.name, stem), edit.tracks});
        case PlaylistEditKind::Rename: {
            PlaylistOutcome out;
            out.ok = false;
            const std::string newName = jvm::trim(edit.newName);  // the repository trims before it asks the store
            const auto lists = listed(folder.fileTexts);
            const MusicPlaylist* current = named(lists, edit.name);
            if (!current) return out;
            if (jvm::isBlank(newName) || named(lists, newName)) return out;
            MusicPlaylist renamed = *current;
            renamed.name = newName;
            out = saved(std::move(renamed));
            if (stem(edit.name) != stem(newName)) out.deleteName = edit.name;
            return out;
        }
        default: break;
    }

    // addTrack, removeTrack and move: the playlist listed under the name, else an empty one while the file the
    // name maps to is absent or readable, else (an unreadable file that must not be overwritten) an empty
    // playlist that is returned and never written.
    MusicPlaylist current{edit.name, {}};
    {
        const auto lists = listed(folder.fileTexts);
        if (const MusicPlaylist* found = named(lists, edit.name)) {
            current = *found;
        } else if (folder.namedFileText && !playlistParse(*folder.namedFileText)) {
            return unchanged({edit.name, {}});
        }
    }
    switch (edit.kind) {
        case PlaylistEditKind::AddTrack:
            if (std::find(current.tracks.begin(), current.tracks.end(), edit.track) == current.tracks.end()) {
                current.tracks.push_back(edit.track);
            }
            return saved(std::move(current));
        case PlaylistEditKind::RemoveTrack:
            current.tracks.erase(std::remove(current.tracks.begin(), current.tracks.end(), edit.track), current.tracks.end());
            return saved(std::move(current));
        case PlaylistEditKind::Move: {
            const int size = static_cast<int>(current.tracks.size());
            if (edit.from < 0 || edit.from >= size) return unchanged(std::move(current));
            const int target = std::clamp(edit.to, 0, size - 1);  // coerceIn(0, size - 1)
            std::string item = std::move(current.tracks[static_cast<size_t>(edit.from)]);
            current.tracks.erase(current.tracks.begin() + edit.from);
            current.tracks.insert(current.tracks.begin() + target, std::move(item));
            return saved(std::move(current));
        }
        default: break;
    }
    return unchanged(std::move(current));
}

std::string playlistUniqueName(const std::vector<std::string>& takenNames, const std::string& base, const StemFn& stem) {
    // Compared by file stem: two names that share one are the same playlist on disk.
    std::set<std::string> taken;
    for (const std::string& name : takenNames) taken.insert(stem(name));
    if (!taken.count(stem(base))) return base;
    int suffix = 2;
    while (taken.count(stem(base + " (" + std::to_string(suffix) + ")"))) ++suffix;
    return base + " (" + std::to_string(suffix) + ")";
}

std::string playlistImportBase(const std::string& parsedName, const std::string& fileName, const std::string& untitled) {
    if (!jvm::isBlank(parsedName)) return parsedName;
    if (!jvm::isBlank(fileName)) return fileName;
    return untitled;
}

std::vector<PlaylistRename> playlistMigrationPlan(const std::vector<PlaylistFileText>& files, const StemFn& stem) {
    std::vector<PlaylistRename> plan;
    std::set<std::string> present;
    for (const auto& f : files) present.insert(f.fileName);
    for (const auto& f : files) {
        if (extensionOf(f.fileName) != "json") continue;
        const auto p = playlistParse(f.text);
        if (!p) continue;
        const std::string s = stem(p->name);
        if (withoutExtension(f.fileName) == s) continue;
        const std::string target = s + ".json";
        if (present.count(target)) continue;
        // File.renameTo: the file now lives under the target, and its old name is free.
        present.erase(f.fileName);
        present.insert(target);
        plan.push_back({f.fileName, target});
    }
    return plan;
}

namespace {

constexpr char kField = '\x1f';
constexpr char kRecord = '\x1e';

void appendText(std::string& out, const std::string& text) {
    for (const char c : text) {
        if (c != kField && c != kRecord) out += c;
    }
}

}  // namespace

std::string playlistWire(const std::vector<MusicPlaylist>& playlists) {
    std::string out;
    for (const MusicPlaylist& p : playlists) {
        appendText(out, p.name);
        for (const std::string& uri : p.tracks) {
            out += kField;
            appendText(out, uri);
        }
        out += kRecord;
    }
    return out;
}

std::string playlistOutcomeWire(const PlaylistOutcome& outcome) {
    std::string out;
    out += outcome.write ? '1' : '0';
    out += kField;
    out += outcome.ok ? '1' : '0';
    out += kField;
    appendText(out, outcome.playlist.name);
    out += kField;
    appendText(out, outcome.deleteName);
    out += kField;
    appendText(out, outcome.json);
    out += kRecord;
    return out;
}

std::string playlistRenameWire(const std::vector<PlaylistRename>& plan) {
    std::string out;
    for (const PlaylistRename& r : plan) {
        appendText(out, r.from);
        out += kField;
        appendText(out, r.to);
        out += kRecord;
    }
    return out;
}

}  // namespace geode::library
