#include "viz/MilkStarterPack.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace geode::viz::milk {

namespace {

namespace fs = std::filesystem;

bool endsWith(const std::string& s, const char* suffix) {
    const std::string tail(suffix);
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// The version the marker holds; 0 when there is none or it is not a whole number.
int readMarker(const fs::path& dir) {
    std::ifstream in(dir / kStarterMarker, std::ios::binary);
    if (!in) return 0;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t b = 0;
    size_t e = text.size();
    while (b < e && isSpace(text[b])) ++b;
    while (e > b && isSpace(text[e - 1])) --e;
    if (b == e) return 0;
    int version = 0;
    const char* first = text.data() + b;
    const char* last = text.data() + e;
    const auto [end, ec] = std::from_chars(first, last, version);
    return ec == std::errc() && end == last ? version : 0;
}

void writeMarker(const fs::path& dir) {
    std::ofstream out(dir / kStarterMarker, std::ios::binary | std::ios::trunc);
    out << kStarterPackVersion;
}

bool copyPreset(StarterSource& source, const std::string& name, const fs::path& destination) {
    std::string bytes;
    bool copied = source.read(name, bytes);
    if (copied) {
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        copied = !out.fail();
    }
    if (!copied) {
        std::error_code ec;
        fs::remove(destination, ec);
    }
    return copied;
}

}  // namespace

int installStarterPack(StarterSource& source, const std::string& dir) {
    const fs::path target(dir);
    if (readMarker(target) >= kStarterPackVersion) return 0;
    std::error_code ec;
    if (!fs::is_directory(target, ec) && !fs::create_directories(target, ec)) return 0;
    int written = 0;
    for (const std::string& name : source.names()) {
        if (!endsWith(name, ".milk")) continue;
        const fs::path destination = target / name;
        if (fs::exists(destination, ec)) continue;
        if (copyPreset(source, name, destination)) ++written;
    }
    writeMarker(target);
    return written;
}

std::string firstRunPreset(StarterSource& source, const std::string& dir) {
    installStarterPack(source, dir);
    const std::string path = (fs::path(dir) / kFirstRunPreset).string();
    return isFile(path) ? path : std::string();
}

bool isFile(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace geode::viz::milk
