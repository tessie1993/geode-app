#include "viz/ShaderSource.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <memory>

namespace geode::viz {

namespace {

constexpr std::array<const char*, 8> kIncludes = {
    "lib_palette", "lib_scene_uniforms", "lib_scene_grade", "lib_sdf3",
    "lib_touch",   "lib_psrdnoise2",     "lib_particle_common", "lib_particle_shade",
};

struct AssetClose {
    void operator()(AAsset* asset) const { AAsset_close(asset); }
};

bool isBlank(char c) { return c == ' ' || c == '\t'; }
bool isWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Matches GlUtil.INCLUDE_PATTERN: ^[ \t]*//#include[ \t]+(\w+)[ \t]*$ on one line.
std::optional<std::string> includeNameOf(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && isBlank(line[i])) i++;
    static constexpr const char* kTag = "//#include";
    const size_t tagLen = std::strlen(kTag);
    if (line.compare(i, tagLen, kTag) != 0) return std::nullopt;
    i += tagLen;
    const size_t afterTag = i;
    while (i < line.size() && isBlank(line[i])) i++;
    if (i == afterTag) return std::nullopt;
    const size_t start = i;
    while (i < line.size() && isWord(line[i])) i++;
    if (i == start) return std::nullopt;
    const std::string name = line.substr(start, i - start);
    while (i < line.size() && isBlank(line[i])) i++;
    if (i != line.size()) return std::nullopt;
    return name;
}

}  // namespace

std::optional<std::string> ShaderSource::load(const std::string& name, std::string* error) const {
    const auto raw = readAsset("shaders/" + name);
    if (!raw) {
        if (error) *error = "missing shader asset '" + name + "'";
        return std::nullopt;
    }
    return resolveIncludes(*raw, error);
}

std::optional<std::string> ShaderSource::readAsset(const std::string& path) const {
    if (!assets_) return std::nullopt;
    std::unique_ptr<AAsset, AssetClose> asset(AAssetManager_open(assets_, path.c_str(), AASSET_MODE_BUFFER));
    if (!asset) return std::nullopt;
    const off_t length = AAsset_getLength(asset.get());
    std::string out(static_cast<size_t>(length), '\0');
    if (length > 0 && AAsset_read(asset.get(), out.data(), static_cast<size_t>(length)) != length) return std::nullopt;
    return out;
}

std::optional<std::string> ShaderSource::resolveIncludes(const std::string& source, std::string* error) const {
    std::string out;
    out.reserve(source.size());
    size_t pos = 0;
    while (pos <= source.size()) {
        const size_t eol = source.find('\n', pos);
        const std::string line = source.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        std::string body = line;
        if (!body.empty() && body.back() == '\r') body.pop_back();
        if (const auto name = includeNameOf(body)) {
            if (!isIncludeName(*name)) {
                if (error) *error = "unknown shader include '" + *name + "'";
                return std::nullopt;
            }
            const auto included = readAsset("shaders/" + *name + ".glsl");
            if (!included) {
                if (error) *error = "missing shader include '" + *name + "'";
                return std::nullopt;
            }
            out += *included;
        } else {
            out += line;
        }
        if (eol == std::string::npos) break;
        out += '\n';
        pos = eol + 1;
    }
    return out;
}

bool ShaderSource::isIncludeName(const std::string& name) {
    for (const char* known : kIncludes) {
        if (name == known) return true;
    }
    return false;
}

}  // namespace geode::viz
