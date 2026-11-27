#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "util/Sha256.hpp"
#include "viz/gl/CacheSentinels.hpp"
#include "viz/gl/GlCaps.hpp"

// Styles that took the process down while they were built or first drawn, remembered per driver.
// A driver that crashes or hangs inside glCompileShader, glLinkProgram or a style's first draws
// returns nothing the build path can catch, and the chosen style is saved, so without this one such
// style crashed the app on every launch. Files only, no GL, so the host tests drive it
// (core/tests/style_quarantine_test.cpp).
//
// Each renderer arms building-<id> before it builds a style it has not seen survive on this driver
// and removes it once the style has drawn kProvenDraws frames (each render begins after the previous
// frame was swapped). A leftover file at the first open in a process means a process died in
// between: a strike. Two strikes turn the style off, so one OEM kill or swipe during a first draw
// does not. The live view, the wallpaper, the dream and Cast each own a renderer, so opening and
// arming go through CacheSentinels exactly as the program-binary cache's sentinel does.
namespace geode::viz {

class StyleQuarantine {
public:
    static constexpr int kStrikes = 2;
    static constexpr uint64_t kProvenDraws = 4;

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
    void open(const std::string& dir, const std::string& renderer, long long nowS) {
        if (open_ || dir.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir, ec)) return;
        dir_ = dir;
        open_ = true;
        sentinels_.open(dir_, [&] { countLeftovers(renderer, nowS); });
        quarantined_ = readLines(dir_ + "/quarantined.txt");
        const auto proven = readLines(dir_ + "/proven.txt");
        proven_.insert(proven.begin(), proven.end());
    }

    bool isOpen() const { return open_; }
    const std::vector<std::string>& quarantined() const { return quarantined_; }
    bool proven(const std::string& id) const { return proven_.count(id) > 0; }

    // Before building `id`. Cheap after the first call: proven and already armed ids touch nothing.
    void arm(const std::string& id) {
        if (!open_ || armed_.count(id) > 0 || proven(id) || isQuarantined(id) || !safeName(id)) return;
        armed_[id] = Armed{};
        const std::string file = sentinel(id);
        sentinels_.arm(file, [&] { std::ofstream(file).close(); });
    }

    // `id` drew in frame `frame`; a second draw in the same frame does not count.
    void drew(const std::string& id, uint64_t frame) {
        if (armed_.empty()) return;
        const auto it = armed_.find(id);
        if (it == armed_.end() || it->second.lastFrame == frame) return;
        it->second.lastFrame = frame;
        if (++it->second.frames < kProvenDraws) return;
        armed_.erase(it);
        disarm(id);
        proven_.insert(id);
        appendLine(dir_ + "/proven.txt", id);
    }

    // Once a frame, for the frame before `frame`: a style that was neither drawn in it nor `building` was
    // left before its draws proved it (picked past, a staging dropped). It stops guarding, or a later
    // swipe or kill of the app would strike a style that was not even on screen.
    void settle(uint64_t frame, const std::string& building) {
        for (auto it = armed_.begin(); it != armed_.end();) {
            if (it->second.lastFrame == frame || it->first == building) {
                ++it;
                continue;
            }
            disarm(it->first);
            it = armed_.erase(it);
        }
    }

    // The builds this renderer still guards ended without a crash (a lost context, a closed view).
    void release() {
        for (const auto& entry : armed_) disarm(entry.first);
        armed_.clear();
    }

private:
    static constexpr uint32_t kSchema = 1;
    static constexpr const char* kPrefix = "building-";

    struct Armed {
        uint64_t frames = 0;
        uint64_t lastFrame = UINT64_MAX;
    };

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

    // One write per line, so two renderers appending at once never interleave inside a line.
    static void appendLine(const std::string& file, const std::string& line) {
        if (std::FILE* f = std::fopen(file.c_str(), "a")) {
            const std::string text = line + "\n";
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }

    std::string sentinel(const std::string& id) const { return dir_ + "/" + kPrefix + id; }

    bool isQuarantined(const std::string& id) const {
        for (const auto& entry : quarantined_) {
            if (entry == id) return true;
        }
        return false;
    }

    void disarm(const std::string& id) {
        const std::string file = sentinel(id);
        sentinels_.disarm(file, [&] { std::remove(file.c_str()); });
    }

    // First open in this process only: every sentinel here is a dead process's.
    void countLeftovers(const std::string& renderer, long long nowS) {
        std::vector<std::string> died;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir_, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.rfind(kPrefix, 0) != 0) continue;
            const std::string id = name.substr(std::char_traits<char>::length(kPrefix));
            if (safeName(id)) died.push_back(id);
        }
        if (died.empty()) return;
        for (const auto& id : died) std::remove(sentinel(id).c_str());

        std::map<std::string, int> strikes;
        {
            std::ifstream in(dir_ + "/strikes.txt");
            std::string id;
            int count = 0;
            while (in >> id >> count) strikes[id] = count;
        }
        const auto already = readLines(dir_ + "/quarantined.txt");
        std::ofstream crash(dir_ + "/last-crash.txt", std::ios::trunc);
        crash << "driver " << renderer << "\ntime " << nowS << "\n";
        for (const auto& id : died) {
            crash << "style " << id << "\n";
            if (++strikes[id] < kStrikes) continue;
            bool listed = false;
            for (const auto& entry : already) listed = listed || entry == id;
            if (!listed) appendLine(dir_ + "/quarantined.txt", id);
        }
        std::ofstream out(dir_ + "/strikes.txt", std::ios::trunc);
        for (const auto& [id, count] : strikes) out << id << " " << count << "\n";
    }

    CacheSentinels& sentinels_;
    std::string dir_;
    bool open_ = false;
    std::vector<std::string> quarantined_;
    std::set<std::string> proven_;
    std::map<std::string, Armed> armed_;
};

}  // namespace geode::viz
