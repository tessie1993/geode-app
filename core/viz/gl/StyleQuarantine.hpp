#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "util/Sha256.hpp"
#include "viz/gl/CacheSentinels.hpp"
#include "viz/gl/GlCaps.hpp"

// Styles that took the process down while they were built or first drawn, remembered per driver.
// A driver that crashes inside glCompileShader, glLinkProgram or a style's first draws returns nothing
// the build path can catch, and the chosen style is saved, so without this one such style crashed the
// app on every launch. Files only, no GL, so the host tests drive it (core/tests/style_quarantine_test.cpp).
//
// Each renderer arms building-<id> before it builds a style not yet proven on this driver, takes it
// down while the surface is paused, and removes it for good once the style has drawn kProvenDraws
// frames over kProvenSeconds. At the first open in a process, a leftover sentinel is a strike only when
// the app says the last process died of a native crash or an ANR (setLastDeath) and it was the only
// style armed. Two strikes turn the style off. The live view, the wallpaper, the dream and Cast each
// own a renderer, so opening and arming go through CacheSentinels as the program-binary cache does.
namespace geode::viz {

class StyleQuarantine {
public:
    static constexpr int kStrikes = 2;
    static constexpr uint64_t kProvenDraws = 4;
    static constexpr double kProvenSeconds = 2.0;

    // How the previous process died, from Android's exit record. Only Crash (native crash or ANR) strikes.
    enum class Death { Unknown, Other, Crash };
    static void setLastDeath(Death death) { lastDeathSlot().store(static_cast<int>(death), std::memory_order_relaxed); }
    static Death lastDeath() { return static_cast<Death>(lastDeathSlot().load(std::memory_order_relaxed)); }

    explicit StyleQuarantine(CacheSentinels& sentinels = CacheSentinels::process()) : sentinels_(sentinels) {}
    ~StyleQuarantine() { release(); }
    StyleQuarantine(const StyleQuarantine&) = delete;
    StyleQuarantine& operator=(const StyleQuarantine&) = delete;

    // A new driver or app build gets a new list: an update may have fixed what crashed.
    static std::string dirFor(const std::string& cacheDir, const GlIdentity& identity, const std::string& build) {
        Sha256 digest;
        digest.updateInt(kSchema);
        digest.updateFramed(identity.vendor);
        digest.updateFramed(identity.renderer);
        digest.updateFramed(identity.versionString);
        digest.updateFramed(build);
        return cacheDir + "/gl-quarantine/" + Sha256::hex(digest.digest(), 8);
    }

    // `renderer` and `nowS` only go into last-crash.txt.
    void open(const std::string& dir, const std::string& renderer, long long nowS, Death death = lastDeath()) {
        if (open_ || dir.empty()) return;
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(dir).parent_path();
        if (parent.filename() == "gl-quarantine") sentinels_.open(parent.string(), [&] { pruneSiblings(parent, dir); });
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir, ec)) return;
        dir_ = dir;
        open_ = true;
        sentinels_.open(dir_, [&] { countLeftovers(renderer, nowS, death); });
        quarantined_ = readLines(dir_ + "/quarantined.txt");
        const auto proven = readLines(dir_ + "/proven.txt");
        proven_.insert(proven.begin(), proven.end());
        if (!proven.empty()) lastProven_ = proven.back();
    }

    bool isOpen() const { return open_; }
    const std::vector<std::string>& quarantined() const { return quarantined_; }
    bool isQuarantined(const std::string& id) const { return std::find(quarantined_.begin(), quarantined_.end(), id) != quarantined_.end(); }
    bool proven(const std::string& id) const { return proven_.count(id) > 0; }
    const std::string& lastProven() const { return lastProven_; }

    // What to draw for `requested`: itself unless it is off, else the last proven style, else `safeDefault`;
    // empty when all of them are off.
    std::string standIn(const std::string& requested, const std::string& safeDefault) const {
        if (!isQuarantined(requested)) return requested;
        if (!lastProven_.empty() && !isQuarantined(lastProven_)) return lastProven_;
        if (!isQuarantined(safeDefault)) return safeDefault;
        return {};
    }

    // Before building `id`. Cheap after the first call: proven and already armed ids touch nothing.
    void arm(const std::string& id) {
        if (!open_ || armed_.count(id) > 0 || proven(id) || isQuarantined(id) || !safeName(id)) return;
        if (!place(id)) return;
        armed_[id].seen = frame_;
    }

    // At the start of each frame. A style neither armed nor drawn in the last frame, nor `building`, was left
    // before its draws proved it, so it stops guarding; one a pause took down is put back.
    void frame(const std::string& building, double nowS) {
        now_ = nowS;
        for (auto it = armed_.begin(); it != armed_.end();) {
            const bool kept = it->second.seen == frame_ || it->first == building;
            if (kept && (it->second.onDisk || place(it->first))) {
                it->second.onDisk = true;
                ++it;
                continue;
            }
            if (it->second.onDisk) disarm(it->first);
            it = armed_.erase(it);
        }
        ++frame_;
    }

    // `id` drew in this frame; a second draw in the same frame does not count.
    void drew(const std::string& id) {
        if (armed_.empty()) return;
        const auto it = armed_.find(id);
        if (it == armed_.end() || it->second.drawn == frame_) return;
        if (sentinels_.settled(sentinel(id))) {
            learnProven(id);
            armed_.erase(it);
            return;
        }
        Armed& a = it->second;
        a.drawn = frame_;
        a.seen = frame_;
        if (a.firstDrawS < 0.0) a.firstDrawS = now_;
        if (++a.frames < kProvenDraws || now_ - a.firstDrawS < kProvenSeconds) return;
        armed_.erase(it);
        prove(id);
    }

    // The surface stops drawing with its context kept: a kill from here on is not the style's doing.
    void pause() {
        for (auto& [id, a] : armed_) {
            if (a.onDisk) disarm(id);
            a.onDisk = false;
            a.frames = 0;
            a.firstDrawS = -1.0;
        }
    }

    // `id` is no longer the built-in style (a shader override draws it): stop guarding it.
    void leave(const std::string& id) {
        const auto it = armed_.find(id);
        if (it == armed_.end()) return;
        if (it->second.onDisk) disarm(id);
        armed_.erase(it);
    }

    // The builds this renderer still guards ended without a crash (a lost context, a closed view).
    void release() {
        for (const auto& [id, a] : armed_) {
            if (a.onDisk) disarm(id);
        }
        armed_.clear();
    }

    // Tests only: the process dies here, its sentinels left on disk.
    void abandon() { armed_.clear(); }

private:
    static constexpr uint32_t kSchema = 1;
    static constexpr const char* kPrefix = "building-";

    struct Armed {
        uint64_t frames = 0;
        uint64_t drawn = UINT64_MAX;
        uint64_t seen = 0;
        double firstDrawS = -1.0;
        bool onDisk = true;
    };

    static std::atomic<int>& lastDeathSlot() {
        static std::atomic<int> slot{static_cast<int>(Death::Unknown)};
        return slot;
    }

    // Style ids are catalogue names; anything else never becomes a path.
    static bool safeName(const std::string& id) {
        if (id.empty() || id.front() == '.') return false;
        for (const char c : id) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!ok) return false;
        }
        return true;
    }

    static std::vector<std::string> readLines(const std::string& file) {
        std::vector<std::string> out;
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            if (safeName(line)) out.push_back(line);
        }
        return out;
    }

    // Written whole to a temp file and renamed over, so a death mid-write leaves the old file or the new.
    static void writeAtomic(const std::string& file, const std::string& text) {
        const std::string temp = file + ".tmp";
        {
            std::ofstream out(temp, std::ios::trunc);
            out << text;
            if (!out.flush()) return;
        }
        std::rename(temp.c_str(), file.c_str());
    }

    // One write per line, so two renderers appending at once never interleave inside a line.
    static void appendLine(const std::string& file, const std::string& line) {
        if (std::FILE* f = std::fopen(file.c_str(), "a")) {
            const std::string text = line + "\n";
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }

    // "<id> <count>" per line; any other line is skipped.
    std::map<std::string, int> readStrikes() const {
        std::map<std::string, int> strikes;
        std::ifstream in(dir_ + "/strikes.txt");
        for (std::string line; std::getline(in, line);) {
            std::istringstream fields(line);
            std::string id;
            int count = 0;
            std::string extra;
            if (!(fields >> id >> count) || (fields >> extra) || !safeName(id) || count <= 0) continue;
            strikes[id] = count;
        }
        return strikes;
    }

    void writeStrikes(const std::map<std::string, int>& strikes) const {
        std::string text;
        for (const auto& [id, count] : strikes) text += id + " " + std::to_string(count) + "\n";
        writeAtomic(dir_ + "/strikes.txt", text);
    }

    std::string sentinel(const std::string& id) const { return dir_ + "/" + kPrefix + id; }

    // Creates (or joins) `id`'s sentinel; false when another renderer already proved it.
    bool place(const std::string& id) {
        const std::string file = sentinel(id);
        if (sentinels_.settled(file)) {
            learnProven(id);
            return false;
        }
        sentinels_.arm(file, [&] { std::ofstream(file).close(); });
        return true;
    }

    void disarm(const std::string& id) {
        const std::string file = sentinel(id);
        sentinels_.disarm(file, [&] { std::remove(file.c_str()); });
    }

    void learnProven(const std::string& id) {
        proven_.insert(id);
        lastProven_ = id;
    }

    // Recorded before the sentinel goes, for every renderer holding it, and its strikes with it.
    void prove(const std::string& id) {
        learnProven(id);
        const std::string file = sentinel(id);
        sentinels_.settle(file, [&] {
            appendLine(dir_ + "/proven.txt", id);
            auto strikes = readStrikes();
            if (strikes.erase(id) > 0) writeStrikes(strikes);
            std::remove(file.c_str());
        });
    }

    // Lists for an older driver or app build are never read again.
    static void pruneSiblings(const std::filesystem::path& parent, const std::string& keep) {
        const std::string name = std::filesystem::path(keep).filename().string();
        std::error_code ec;
        std::vector<std::filesystem::path> old;
        for (std::filesystem::directory_iterator it(parent, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (it->path().filename() != name) old.push_back(it->path());
        }
        for (const auto& path : old) std::filesystem::remove_all(path, ec);
    }

    // First open in this process only: every sentinel here is a dead process's.
    void countLeftovers(const std::string& renderer, long long nowS, Death death) {
        std::vector<std::string> died;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir_, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.rfind(kPrefix, 0) != 0) continue;
            const std::string id = name.substr(std::char_traits<char>::length(kPrefix));
            if (safeName(id)) died.push_back(id);
        }
        if (died.empty()) return;
        std::sort(died.begin(), died.end());

        const auto provenList = readLines(dir_ + "/proven.txt");
        const std::set<std::string> provenSet(provenList.begin(), provenList.end());
        std::vector<std::string> blamed;
        for (const auto& id : died) {
            if (provenSet.count(id) == 0) blamed.push_back(id);
        }
        if (death == Death::Crash && !blamed.empty()) {
            std::string crash = "driver " + renderer + "\ntime " + std::to_string(nowS) + "\n";
            if (blamed.size() == 1) {
                const std::string& id = blamed.front();
                auto strikes = readStrikes();
                const int count = ++strikes[id];
                writeStrikes(strikes);
                if (count >= kStrikes) {
                    auto listed = readLines(dir_ + "/quarantined.txt");
                    if (std::find(listed.begin(), listed.end(), id) == listed.end()) {
                        listed.push_back(id);
                        std::string text;
                        for (const auto& entry : listed) text += entry + "\n";
                        writeAtomic(dir_ + "/quarantined.txt", text);
                    }
                }
                crash += "style " + id + "\n";
            } else {
                crash += "ambiguous";
                for (const auto& id : blamed) crash += " " + id;
                crash += "\n";
            }
            writeAtomic(dir_ + "/last-crash.txt", crash);
        }
        for (const auto& id : died) std::remove(sentinel(id).c_str());
    }

    CacheSentinels& sentinels_;
    std::string dir_;
    bool open_ = false;
    std::vector<std::string> quarantined_;
    std::set<std::string> proven_;
    std::string lastProven_;
    std::map<std::string, Armed> armed_;
    uint64_t frame_ = 0;
    double now_ = 0.0;
};

}  // namespace geode::viz
