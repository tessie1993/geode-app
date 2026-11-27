#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "util/Sha256.hpp"
#include "viz/gl/CacheSentinels.hpp"
#include "viz/gl/GlCaps.hpp"

// Styles that took the process down while they were built or first drawn, remembered per driver.
// A driver that crashes inside glCompileShader, glLinkProgram or a style's first draws returns nothing
// the build path can catch, and the chosen style is saved, so without this one such style crashed the
// app on every launch. Files only, no GL, so the host tests drive it (core/tests/style_quarantine_test.cpp).
//
// Each renderer arms building-<id> (holding its pid and the time) before it builds a style not yet
// proven on this driver, takes it down while the surface is paused, and removes it for good once the
// style has drawn kProvenDraws frames over kProvenSeconds. The first open in a process sets leftovers
// aside as left-<pid>-<ms>-<id> and touches nothing else until the app says how earlier processes died
// (StyleDeaths::tell). A leftover is a strike only when the exit record of its own process, written after
// it, is a crash in the graphics driver (or an ANR waiting on the stage) and it was that process's only
// style armed. Two strikes turn the style off. The live view, the wallpaper, the dream and Cast each own a
// renderer, so opening and arming go through CacheSentinels as the program-binary cache does.
namespace geode::viz {

// How earlier processes of the app died, as the app read Android's exit records, and the quarantine
// lists open in this process. Tests make one per simulated process.
class StyleDeaths {
public:
    enum class Verdict { Unknown, Other, Crash };
    struct Death {
        int pid = 0;
        long long timeMs = 0;
        Verdict verdict = Verdict::Unknown;
    };
    using Resolve = std::function<void(const std::vector<Death>&)>;

    static StyleDeaths& process() {
        static StyleDeaths instance(static_cast<int>(::getpid()));
        return instance;
    }

    explicit StyleDeaths(int pid) : pid_(pid) {}
    StyleDeaths(const StyleDeaths&) = delete;
    StyleDeaths& operator=(const StyleDeaths&) = delete;

    int pid() const { return pid_; }

    // The app's word, once: the lists open now and those opened later are settled against it.
    void tell(std::vector<Death> deaths) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (told_) return;
        told_ = true;
        deaths_ = std::move(deaths);
        for (auto& [dir, resolve] : lists_) resolve(deaths_);
        generation_.fetch_add(1, std::memory_order_release);
    }

    // Moves when a tell may have quarantined a style in a list already open.
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    // The first open of `dir` in this process.
    void opened(const std::string& dir, Resolve resolve) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lists_.count(dir) > 0) return;
        if (told_) resolve(deaths_);
        lists_.emplace(dir, std::move(resolve));
    }

private:
    const int pid_;
    std::mutex mutex_;
    bool told_ = false;
    std::vector<Death> deaths_;
    std::map<std::string, Resolve> lists_;
    std::atomic<uint64_t> generation_{0};
};

class StyleQuarantine {
public:
    static constexpr int kStrikes = 2;
    static constexpr uint64_t kProvenDraws = 4;
    static constexpr double kProvenSeconds = 2.0;

    explicit StyleQuarantine(CacheSentinels& sentinels = CacheSentinels::process(), StyleDeaths& deaths = StyleDeaths::process())
        : sentinels_(sentinels), deaths_(deaths) {}
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
        const std::filesystem::path parent = std::filesystem::path(dir).parent_path();
        if (parent.filename() == "gl-quarantine") sentinels_.open(parent.string(), [&] { pruneSiblings(parent, dir); });
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir, ec)) return;
        dir_ = dir;
        open_ = true;
        generation_ = deaths_.generation();
        sentinels_.open(dir_, [&] { setAside(dir_); });
        deaths_.opened(dir_, [&sentinels = sentinels_, dir = dir_, renderer, nowS](const std::vector<StyleDeaths::Death>& deaths) {
            sentinels.locked([&] { resolve(dir, renderer, nowS, deaths); });
        });
        quarantined_ = readLines(dir_ + "/quarantined.txt");
        const auto proven = readLines(dir_ + "/proven.txt");
        proven_.insert(proven.begin(), proven.end());
    }

    bool isOpen() const { return open_; }
    const std::vector<std::string>& quarantined() const { return quarantined_; }
    bool isQuarantined(const std::string& id) const { return std::find(quarantined_.begin(), quarantined_.end(), id) != quarantined_.end(); }
    bool proven(const std::string& id) const { return proven_.count(id) > 0; }

    // Before building `id`. Cheap after the first call: proven and already armed ids touch nothing.
    void arm(const std::string& id) {
        if (!open_ || armed_.count(id) > 0 || proven(id) || isQuarantined(id) || !safeName(id)) return;
        if (!place(id)) return;
        armed_[id].seen = frame_;
    }

    // At the start of each frame; true when the quarantined list grew since the last look (the app's word on
    // how earlier processes died came after open()). A style neither armed nor drawn in the last frame, nor `building`, was left
    // before its draws proved it, so it stops guarding. One a pause took down goes back up once it draws on
    // (one draw while paused, a still frame, does not), or at once if it never drew.
    bool frame(const std::string& building, double nowS) {
        now_ = nowS;
        const bool grew = open_ && refresh();
        for (auto it = armed_.begin(); it != armed_.end();) {
            Armed& a = it->second;
            const bool kept = a.seen == frame_ || it->first == building;
            if (kept && !a.onDisk && (!a.everDrawn || a.drawnSincePause)) {
                if (!place(it->first)) {
                    it = armed_.erase(it);
                    continue;
                }
                a.onDisk = true;
            }
            if (kept) {
                ++it;
                continue;
            }
            if (a.onDisk) disarm(it->first);
            it = armed_.erase(it);
        }
        ++frame_;
        return grew;
    }

    // `id` drew in this frame; a second draw in the same frame does not count.
    void drew(const std::string& id) {
        if (armed_.empty()) return;
        const auto it = armed_.find(id);
        if (it == armed_.end() || it->second.drawn == frame_) return;
        if (sentinels_.settled(sentinel(id))) {
            proven_.insert(id);
            armed_.erase(it);
            return;
        }
        Armed& a = it->second;
        a.drawn = frame_;
        a.seen = frame_;
        if (a.everDrawn && !a.onDisk) a.drawnSincePause = true;
        a.everDrawn = true;
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
            a.drawnSincePause = false;
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
    static constexpr const char* kLeft = "left-";

    bool refresh() {
        const uint64_t generation = deaths_.generation();
        if (generation == generation_) return false;
        generation_ = generation;
        auto listed = readLines(dir_ + "/quarantined.txt");
        if (listed.size() <= quarantined_.size()) return false;
        quarantined_ = std::move(listed);
        for (const auto& id : quarantined_) leave(id);
        return true;
    }

    struct Armed {
        uint64_t frames = 0;
        uint64_t drawn = UINT64_MAX;
        uint64_t seen = 0;
        double firstDrawS = -1.0;
        bool onDisk = true;
        bool everDrawn = false;
        bool drawnSincePause = false;
    };

    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
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

    // Written whole to a temp file, synced and renamed over, so a death or a power cut leaves the old file
    // or the new.
    static bool writeAtomic(const std::string& file, const std::string& text) {
        const std::string temp = file + ".tmp";
        const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        bool ok = true;
        for (size_t done = 0; ok && done < text.size();) {
            const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
            if (n > 0) {
                done += static_cast<size_t>(n);
            } else if (n < 0 && errno != EINTR) {
                ok = false;
            }
        }
        ok = ::fsync(fd) == 0 && ok;
        ::close(fd);
        if (!ok || std::rename(temp.c_str(), file.c_str()) != 0) {
            std::remove(temp.c_str());
            return false;
        }
        return true;
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
    static std::map<std::string, int> readStrikes(const std::string& dir) {
        std::map<std::string, int> strikes;
        std::ifstream in(dir + "/strikes.txt");
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

    static void writeStrikes(const std::string& dir, const std::map<std::string, int>& strikes) {
        std::string text;
        for (const auto& [id, count] : strikes) text += id + " " + std::to_string(count) + "\n";
        writeAtomic(dir + "/strikes.txt", text);
    }

    std::string sentinel(const std::string& id) const { return dir_ + "/" + kPrefix + id; }

    // Creates (or joins) `id`'s sentinel; false when another renderer already proved it.
    bool place(const std::string& id) {
        const std::string file = sentinel(id);
        if (sentinels_.settled(file)) {
            proven_.insert(id);
            return false;
        }
        const std::string stamp = std::to_string(deaths_.pid()) + " " + std::to_string(nowMs()) + "\n";
        sentinels_.arm(file, [&] { std::ofstream(file) << stamp; });
        return true;
    }

    void disarm(const std::string& id) {
        const std::string file = sentinel(id);
        sentinels_.disarm(file, [&] { std::remove(file.c_str()); });
    }

    // Recorded before the sentinel goes, for every renderer holding it, and its strikes with it.
    void prove(const std::string& id) {
        proven_.insert(id);
        const std::string file = sentinel(id);
        sentinels_.settle(file, [&] {
            appendLine(dir_ + "/proven.txt", id);
            auto strikes = readStrikes(dir_);
            if (strikes.erase(id) > 0) writeStrikes(dir_, strikes);
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

    static std::vector<std::string> namesIn(const std::string& dir, const char* prefix) {
        std::vector<std::string> out;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.rfind(prefix, 0) == 0) out.push_back(name);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // First open in this process only: every building- file here is a dead process's. It is kept, under a name
    // no live renderer arms, until the app says how that process died.
    static void setAside(const std::string& dir) {
        for (const std::string& name : namesIn(dir, kPrefix)) {
            const std::string id = name.substr(std::char_traits<char>::length(kPrefix));
            const std::string from = dir + "/" + name;
            long long pid = 0;
            long long ms = 0;
            std::ifstream(from) >> pid >> ms;
            if (!safeName(id) || pid <= 0 || ms <= 0) {
                std::remove(from.c_str());
                continue;
            }
            const std::string to = dir + "/" + kLeft + std::to_string(pid) + "-" + std::to_string(ms) + "-" + id;
            std::rename(from.c_str(), to.c_str());
        }
    }

    // Settles every leftover against the exit records: a crash strikes, an Unknown record waits for a later
    // process, anything else (another reason, no record of its own process) is dropped.
    static void resolve(const std::string& dir, const std::string& renderer, long long nowS, const std::vector<StyleDeaths::Death>& deaths) {
        const auto provenList = readLines(dir + "/proven.txt");
        const std::set<std::string> provenSet(provenList.begin(), provenList.end());
        std::map<int, std::vector<std::string>> crashed;
        std::vector<std::string> gone;
        for (const std::string& name : namesIn(dir, kLeft)) {
            const std::string path = dir + "/" + name;
            long long pid = 0;
            long long ms = 0;
            char dash1 = 0;
            char dash2 = 0;
            std::istringstream fields(name.substr(std::char_traits<char>::length(kLeft)));
            std::string id;
            const bool parsed = static_cast<bool>(fields >> pid >> dash1 >> ms >> dash2) && dash1 == '-' && dash2 == '-' &&
                                static_cast<bool>(std::getline(fields, id)) && safeName(id);
            const auto death = std::find_if(deaths.begin(), deaths.end(), [&](const auto& d) { return d.pid == pid && d.timeMs > ms; });
            if (parsed && death != deaths.end() && death->verdict == StyleDeaths::Verdict::Unknown) continue;
            gone.push_back(path);
            if (parsed && death != deaths.end() && death->verdict == StyleDeaths::Verdict::Crash && provenSet.count(id) == 0) {
                crashed[death->pid].push_back(id);
            }
        }
        if (!crashed.empty()) {
            std::string crash = "driver " + renderer + "\ntime " + std::to_string(nowS) + "\n";
            for (const auto& [pid, ids] : crashed) {
                if (ids.size() == 1) {
                    strike(dir, ids.front());
                    crash += "style " + ids.front() + "\n";
                } else {
                    crash += "ambiguous";
                    for (const auto& id : ids) crash += " " + id;
                    crash += "\n";
                }
            }
            writeAtomic(dir + "/last-crash.txt", crash);
        }
        for (const auto& path : gone) std::remove(path.c_str());
    }

    static void strike(const std::string& dir, const std::string& id) {
        auto strikes = readStrikes(dir);
        const int count = ++strikes[id];
        writeStrikes(dir, strikes);
        if (count < kStrikes) return;
        auto listed = readLines(dir + "/quarantined.txt");
        if (std::find(listed.begin(), listed.end(), id) != listed.end()) return;
        listed.push_back(id);
        std::string text;
        for (const auto& entry : listed) text += entry + "\n";
        writeAtomic(dir + "/quarantined.txt", text);
    }

    CacheSentinels& sentinels_;
    StyleDeaths& deaths_;
    std::string dir_;
    bool open_ = false;
    std::vector<std::string> quarantined_;
    std::set<std::string> proven_;
    std::map<std::string, Armed> armed_;
    uint64_t frame_ = 0;
    double now_ = 0.0;
    uint64_t generation_ = 0;
};

}  // namespace geode::viz
