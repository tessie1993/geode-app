#include "viz/MilkMemory.hpp"

#include <algorithm>

namespace geode::viz::milk {

void Memory::seed(const std::string& saved) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (seeded_) return;
    seeded_ = true;
    shown_ = saved;
}

void Memory::request(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    requested_ = path;
}

bool Memory::loaded(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    seeded_ = true;
    if (shown_ == path) return false;
    shown_ = path;
    return true;
}

bool Memory::forget(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(mutex_);
    seeded_ = true;
    const auto listed = [&paths](const std::string& p) {
        return !p.empty() && std::find(paths.begin(), paths.end(), p) != paths.end();
    };
    if (listed(requested_)) requested_.clear();
    if (!listed(shown_)) return false;
    shown_.clear();
    return true;
}

std::string Memory::lastShown(const Usable& usable) const {
    std::string shown;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shown = shown_;
    }
    return !shown.empty() && usable(shown) ? shown : std::string();
}

std::string Memory::restorePath(const Usable& usable, const FirstRun& firstRun) const {
    std::string shown;
    std::string requested;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shown = shown_;
        requested = requested_;
    }
    if (!shown.empty() && usable(shown)) return shown;
    if (!requested.empty() && usable(requested)) return requested;
    return firstRun();
}

}  // namespace geode::viz::milk
