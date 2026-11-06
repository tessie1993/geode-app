#include "viz/ShaderOverrides.hpp"

#include <atomic>
#include <mutex>

namespace geode::viz::shader_overrides {

namespace {

std::mutex& tableLock() {
    static std::mutex m;
    return m;
}

std::map<std::string, std::string>& table() {
    static std::map<std::string, std::string> t;
    return t;
}

// Read every frame by every renderer, so it is an atomic outside the lock; bumped under the lock, so a
// reader that sees the new value and then takes the lock reads the new table.
std::atomic<uint64_t>& counter() {
    static std::atomic<uint64_t> c{0};
    return c;
}

void bump() { counter().fetch_add(1, std::memory_order_acq_rel); }

}  // namespace

void set(const std::string& styleId, const std::string& source) {
    if (styleId.empty()) return;
    std::lock_guard<std::mutex> guard(tableLock());
    auto& t = table();
    if (source.empty()) {
        if (t.erase(styleId) > 0) bump();
        return;
    }
    const auto it = t.find(styleId);
    if (it != t.end() && it->second == source) return;
    t[styleId] = source;
    bump();
}

void clear(const std::string& styleId) { set(styleId, std::string()); }

std::string get(const std::string& styleId) {
    std::lock_guard<std::mutex> guard(tableLock());
    const auto& t = table();
    const auto it = t.find(styleId);
    return it == t.end() ? std::string() : it->second;
}

uint64_t generation() { return counter().load(std::memory_order_acquire); }

std::map<std::string, std::string> snapshot() {
    std::lock_guard<std::mutex> guard(tableLock());
    return table();
}

void clearAll() {
    std::lock_guard<std::mutex> guard(tableLock());
    if (table().empty()) return;
    table().clear();
    bump();
}

}  // namespace geode::viz::shader_overrides
