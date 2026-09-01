#include "viz/TransitionCatalog.hpp"

#include <algorithm>

#include "util/Json.hpp"
#include "util/Log.hpp"

namespace geode::viz {

namespace {

constexpr const char* kAsset = "gl_transitions.json";
constexpr const char* kSourceMarker = "// __GL_TRANSITION_SOURCE__";
constexpr const char* kTag = "Transitions";

std::vector<float> floatsOf(const json::Value* value) {
    if (!value) return {0.0f};
    switch (value->kind()) {
        case json::Value::Kind::Bool: return {value->boolValue() ? 1.0f : 0.0f};
        case json::Value::Kind::Number: return {static_cast<float>(value->number())};
        case json::Value::Kind::Array: {
            std::vector<float> out;
            for (const auto& v : value->array()) out.push_back(v.kind() == json::Value::Kind::Number ? static_cast<float>(v.number()) : 0.0f);
            return out;
        }
        default: return {0.0f};
    }
}

float at(const std::vector<float>& v, size_t i) { return i < v.size() ? v[i] : 0.0f; }

}  // namespace

const std::vector<TransitionCatalog::Def>& TransitionCatalog::library() {
    if (loaded_) return library_;
    loaded_ = true;
    const auto text = assets_.readAsset(kAsset);
    const auto parsed = text ? json::parse(*text) : std::nullopt;
    if (!parsed || parsed->kind() != json::Value::Kind::Array) return library_;
    for (const auto& o : parsed->array()) {
        if (o.kind() != json::Value::Kind::Object) continue;
        Def def;
        def.name = o.optString("name");
        def.author = o.optString("author");
        def.license = o.optString("license");
        def.glsl = o.optString("glsl");
        const json::Value* types = o.get("paramsTypes");
        const json::Value* defaults = o.get("defaultParams");
        if (types && types->kind() == json::Value::Kind::Object) {
            for (const auto& [key, type] : types->object()) {
                Param p;
                p.name = key;
                p.type = type.kind() == json::Value::Kind::String ? type.string() : "float";
                p.values = floatsOf(defaults ? defaults->get(key) : nullptr);
                def.params.push_back(std::move(p));
            }
        }
        byName_[def.name] = library_.size();
        library_.push_back(std::move(def));
    }
    return library_;
}

std::vector<std::string> TransitionCatalog::allIds() {
    std::vector<std::string> ids;
    for (int i = 0; i <= static_cast<int>(TransitionStyle::Zoom); i++) ids.emplace_back(transitionStyleId(static_cast<TransitionStyle>(i)));
    for (const auto& def : library()) ids.push_back(def.name);
    return ids;
}

const TransitionCatalog::Def* TransitionCatalog::definition(const std::string& id) {
    if (builtIn(id)) return nullptr;
    library();
    const auto it = byName_.find(id);
    return it == byName_.end() ? nullptr : &library_[it->second];
}

std::optional<TransitionStyle> TransitionCatalog::builtIn(const std::string& id) {
    for (int i = 0; i <= static_cast<int>(TransitionStyle::Zoom); i++) {
        const auto style = static_cast<TransitionStyle>(i);
        if (id == transitionStyleId(style)) return style;
    }
    return std::nullopt;
}

std::string TransitionCatalog::spliceInto(const std::string& base, const Def& def) {
    const size_t versionEnd = base.find('\n') + 1;
    std::string withDefine = base.substr(0, versionEnd) + "#define MV_TRANSITION 1\n" + base.substr(versionEnd);
    const size_t marker = withDefine.find(kSourceMarker);
    if (marker != std::string::npos) withDefine.replace(marker, std::char_traits<char>::length(kSourceMarker), def.glsl);
    return withDefine;
}

void TransitionCatalog::uploadParams(GLuint program, const Def& def) {
    for (const auto& p : def.params) {
        const GLint loc = glGetUniformLocation(program, p.name.c_str());
        if (loc < 0) continue;
        const auto& v = p.values;
        if (p.type == "float") glUniform1f(loc, at(v, 0));
        else if (p.type == "int" || p.type == "bool") glUniform1i(loc, static_cast<GLint>(at(v, 0)));
        else if (p.type == "vec2") glUniform2f(loc, at(v, 0), at(v, 1));
        else if (p.type == "vec3") glUniform3f(loc, at(v, 0), at(v, 1), at(v, 2));
        else if (p.type == "vec4") glUniform4f(loc, at(v, 0), at(v, 1), at(v, 2), at(v, 3));
        else if (p.type == "ivec2") glUniform2i(loc, static_cast<GLint>(at(v, 0)), static_cast<GLint>(at(v, 1)));
        else if (p.type == "ivec3") glUniform3i(loc, static_cast<GLint>(at(v, 0)), static_cast<GLint>(at(v, 1)), static_cast<GLint>(at(v, 2)));
    }
}

bool TransitionPrograms::create(const std::string& fadeVert, std::string* error) {
    release();
    fadeVert_ = fadeVert;
    const auto source = assets_.load("composite_frag.glsl", error);
    if (!source) return false;
    source_ = *source;
    const GLuint program = program::build(fadeVert_, source_, cache_, error);
    if (program == 0) return false;
    base_ = UniformCache(program);
    return true;
}

void TransitionPrograms::release() {
    if (base_.program() != 0) glDeleteProgram(base_.program());
    base_ = UniformCache(0);
    for (auto& entry : lru_) glDeleteProgram(entry.second->program());
    lru_.clear();
    uploadedFor_ = nullptr;
    uploadedDef_ = nullptr;
}

UniformCache& TransitionPrograms::programFor(const std::string& id) {
    if (TransitionCatalog::builtIn(id)) return base_;
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        if (it->first == id) {
            lru_.splice(lru_.end(), lru_, it);
            return *lru_.back().second;
        }
    }
    const TransitionCatalog::Def* def = definition(id);
    if (!def) return base_;
    std::string error;
    const GLuint program = program::build(fadeVert_, TransitionCatalog::spliceInto(source_, *def), cache_, &error);
    if (program == 0) {
        GEODE_LOGW(kTag, "\"%s\" failed to link: %s", id.c_str(), error.c_str());
        return base_;
    }
    while (lru_.size() >= kMaxPrograms) {
        glDeleteProgram(lru_.front().second->program());
        lru_.pop_front();
    }
    lru_.emplace_back(id, std::make_unique<UniformCache>(program));
    return *lru_.back().second;
}

void TransitionPrograms::warm(const std::string& id) {
    if (!source_.empty()) programFor(id);
}

void TransitionPrograms::uploadParamsIfNeeded(UniformCache& program, const TransitionCatalog::Def* def) {
    if (def && (&program != uploadedFor_ || def != uploadedDef_)) {
        TransitionCatalog::uploadParams(program.program(), *def);
        uploadedFor_ = &program;
        uploadedDef_ = def;
    }
}

}  // namespace geode::viz
