#pragma once
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace geode::viz {

// Process-wide bookkeeping for ProgramBinaryCache's crash sentinel (#381). No GL and no files, so the
// host can test it.
//
// The sentinel is a file that exists only while glProgramBinary runs. Seeing it at open time means a
// previous process died inside the driver, so the cache turns itself off for that driver for good.
// That inference holds only for the first renderer in a process to open the store. The live view,
// the wallpaper, the dream and Cast each own a Renderer, and every Renderer owns a cache on the same
// directory. A second renderer that opened while the first one was loading used to read the first
// one's sentinel as a crash, wipe the store and disable it. Once a process has opened a namespace,
// any sentinel there belongs to a live renderer in this process: a crash would have taken the
// process with it.
//
// Arming is counted for the same reason. Two renderers loading at once share one sentinel file. It is
// created when the first starts and removed when the last ends, so one renderer's finish never
// unguards the other's load.
class CacheSentinels {
public:
    static CacheSentinels& process() {
        static CacheSentinels instance;
        return instance;
    }

    // Opens a namespace for one cache instance. `check` runs under the lock only for the first open
    // in this process, so the crash test and the prune happen once and never race a live load.
    // Returns whether this was the first open.
    template <typename Check>
    bool open(const std::string& ns, Check&& check) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!opened_.insert(ns).second) return false;
        check();
        return true;
    }

    // `create` runs under the lock when this is the first holder. Returns whether it ran.
    template <typename Create>
    bool arm(const std::string& sentinel, Create&& create) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (holders_[sentinel]++ != 0) return false;
        create();
        return true;
    }

    // `remove` runs under the lock when this was the last holder. Returns whether it ran. The file
    // work stays under the lock so a new holder can't create the file just before the last one
    // removes it.
    template <typename Remove>
    bool disarm(const std::string& sentinel, Remove&& remove) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = holders_.find(sentinel);
        if (it == holders_.end()) return false;
        if (--it->second > 0) return false;
        holders_.erase(it);
        remove();
        return true;
    }

    // Tests only: forget everything, as a new process would.
    void resetForTest() {
        std::lock_guard<std::mutex> lock(mutex_);
        opened_.clear();
        holders_.clear();
    }

private:
    std::mutex mutex_;
    std::set<std::string> opened_;
    std::map<std::string, int> holders_;
};

}  // namespace geode::viz
