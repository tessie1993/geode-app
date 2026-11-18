#include "api/geode_milk_api.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "viz/LookFiles.hpp"
#include "viz/MilkMemory.hpp"
#include "viz/MilkStarterPack.hpp"
#include "viz/MilkTextures.hpp"
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

// A list as entries each followed by '\n'.
const char* lines(std::string& slot, const std::vector<std::string>& entries) {
    slot.clear();
    for (const std::string& e : entries) slot += e + "\n";
    return slot.c_str();
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

long long geode_milk_texture_max_bytes(void) { return milk::kTextureMaxBytes; }

int geode_milk_texture_admits(const char* name, long long declared_bytes) {
    try {
        return milk::admitsTexture(text(name), declared_bytes) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

const char* geode_milk_texture_stored_name(const char* name, const unsigned char* bytes, long long length, int width,
                                           int height) {
    thread_local std::string slot;
    try {
        const size_t n = length > 0 ? static_cast<size_t>(length) : 0;
        return answer(slot, milk::acceptedTextureName(text(name), bytes, n, width, height));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_list(const char* texture_dir) {
    thread_local std::string slot;
    try {
        return lines(slot, milk::listStoreTextures(text(texture_dir)));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_removed_presets(const char* texture, const char* texture_dir) {
    thread_local std::string slot;
    try {
        return lines(slot, milk::removedPresetNames(text(texture), milk::listStoreTextures(text(texture_dir))));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_show_preset_name(const char* texture) {
    thread_local std::string slot;
    try {
        slot = milk::showPresetName(text(texture));
        return slot.c_str();
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_show_preset_text(const char* texture) {
    thread_local std::string slot;
    try {
        slot = milk::showPresetText(text(texture));
        return slot.c_str();
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_links(const char* preset_path, const char* texture_dir, const char* const* override_keys,
                                     const char* const* override_values, int override_count) {
    thread_local std::string slot;
    try {
        milk::Overrides overrides;
        for (int i = 0; override_keys && override_values && i < override_count; ++i) {
            overrides.emplace(text(override_keys[i]), text(override_values[i]));
        }
        const std::vector<milk::Link> links =
            milk::resolveAll(milk::readFile(text(preset_path)), milk::listStoreTextures(text(texture_dir)), overrides);
        std::vector<std::string> entries{milk::needsYou(links) ? "1" : "0"};
        for (const milk::Link& link : links) {
            entries.push_back(link.expected);
            entries.push_back(link.texture);
            entries.push_back(std::to_string(static_cast<int>(link.kind)));
            entries.push_back(link.linkNames.size() > 0 ? link.linkNames[0] : std::string());
            entries.push_back(link.linkNames.size() > 1 ? link.linkNames[1] : std::string());
        }
        return lines(slot, entries);
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_linked_presets(const char* milk_dir) {
    thread_local std::string slot;
    try {
        return lines(slot, milk::linkedPresets(text(milk_dir)));
    } catch (...) {
        return nullptr;
    }
}

const char* geode_milk_texture_stale_link_dirs(const char* milk_dir) {
    thread_local std::string slot;
    try {
        return lines(slot, milk::staleLinkDirs(text(milk_dir)));
    } catch (...) {
        return nullptr;
    }
}

int geode_milk_pack_entry_kind(const char* name) {
    try {
        return static_cast<int>(milk::packEntryKind(text(name)));
    } catch (...) {
        return 0;
    }
}

const char* geode_milk_pack_target_name(const char* name) {
    thread_local std::string slot;
    try {
        const std::string n = text(name);
        slot = milk::packEntryKind(n) == milk::PackEntry::Preset ? geode::viz::files::milkFileName(n) : milk::packTextureName(n);
        return slot.c_str();
    } catch (...) {
        return nullptr;
    }
}

int geode_milk_preset_misses_texture(const char* preset_path, const char* texture_dir) {
    try {
        return milk::missesTexture(milk::readFile(text(preset_path)), milk::listEntries(text(texture_dir))) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

const char* geode_milk_import_file_name(const char* milk_dir, const char* display_name, int keep_existing) {
    thread_local std::string slot;
    try {
        const std::filesystem::path dir(text(milk_dir));
        const std::string sanitised = geode::viz::files::milkFileName(milk::importDisplayName(text(display_name)));
        const auto taken = [&dir](const std::string& name) {
            std::error_code ec;
            return std::filesystem::exists(dir / name, ec);
        };
        slot = milk::importFileName(sanitised, keep_existing != 0, taken);
        return slot.c_str();
    } catch (...) {
        return nullptr;
    }
}

}  // extern "C"
