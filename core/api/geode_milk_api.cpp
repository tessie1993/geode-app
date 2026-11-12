#include "api/geode_milk_api.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "viz/MilkMemory.hpp"
#include "viz/MilkStarterPack.hpp"
#include "viz/android/AssetStarterSource.hpp"

namespace {

namespace milk = geode::viz::milk;

milk::Memory& memory() {
    static milk::Memory m;
    return m;
}

// The app's list, the first launch's restore and every surface's GL thread can install at once; one at a
// time, so no two copy the same file.
std::mutex& installLock() {
    static std::mutex m;
    return m;
}

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

const char* answer(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.empty() ? nullptr : slot.c_str();
}

}  // namespace

extern "C" {

void geode_milk_memory_seed(const char* saved) {
    try {
        memory().seed(text(saved));
    } catch (...) {
    }
}

void geode_milk_memory_request(const char* path) {
    try {
        memory().request(text(path));
    } catch (...) {
    }
}

int geode_milk_memory_loaded(const char* path) {
    try {
        return memory().loaded(text(path)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int geode_milk_memory_forget(const char* const* paths, int count) {
    try {
        std::vector<std::string> gone;
        for (int i = 0; paths && i < count; ++i) gone.push_back(text(paths[i]));
        return memory().forget(gone) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

const char* geode_milk_memory_last_shown(void) {
    thread_local std::string slot;
    try {
        return answer(slot, memory().lastShown(milk::isFile));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_memory_restore_path(AAssetManager* assets, const char* milk_dir) {
    thread_local std::string slot;
    try {
        const std::string dir = text(milk_dir);
        const auto firstRun = [assets, &dir]() {
            if (dir.empty()) return std::string();
            std::lock_guard<std::mutex> lock(installLock());
            milk::AssetStarterSource source(assets);
            return milk::firstRunPreset(source, dir);
        };
        return answer(slot, memory().restorePath(milk::isFile, firstRun));
    } catch (...) {
        return nullptr;
    }
}

int geode_milk_starter_install(AAssetManager* assets, const char* milk_dir) {
    try {
        if (!milk_dir || !*milk_dir) return 0;
        std::lock_guard<std::mutex> lock(installLock());
        milk::AssetStarterSource source(assets);
        return milk::installStarterPack(source, milk_dir);
    } catch (...) {
        return 0;
    }
}

}  // extern "C"
