#include "viz/compute/SimGlsl.hpp"

#include <sstream>

namespace geode::viz::sim::glsl {

namespace {

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string loadExpression(const StateEncoding& e, const std::string& fetch) {
    if (e.packed) return "uintBitsToFloat(" + fetch + ")";
    if (e.stateScale != 1.0f) return fetch + " * SIM_STATE_SCALE";
    return fetch;
}

std::string storeExpression(const StateEncoding& e, const std::string& value) {
    if (e.packed) return "floatBitsToUint(" + value + ")";
    if (e.stateScale != 1.0f) return "(" + value + ") / SIM_STATE_SCALE";
    return value;
}

void appendSampler(std::ostringstream& out, const StateEncoding& e, bool sampledInFragmentStage) {
    out << "vec4 simSample(vec2 uv) {\n";
    if (e.filterable) {
        // Outside the fragment stage the implicit LOD is undefined, so name level 0 there.
        const std::string fetch = sampledInFragmentStage ? std::string("texture(") + kUniformState + ", uv)"
                                                         : std::string("textureLod(") + kUniformState + ", uv, 0.0)";
        out << "    return " << loadExpression(e, fetch) << ";\n";
    } else {
        out << "    vec2 p = uv * vec2(" << kUniformSize << ") - 0.5;\n"
            << "    vec2 f = fract(p);\n"
            << "    ivec2 b = ivec2(floor(p));\n"
            << "    vec4 s00 = simLoad(b);\n"
            << "    vec4 s10 = simLoad(b + ivec2(1, 0));\n"
            << "    vec4 s01 = simLoad(b + ivec2(0, 1));\n"
            << "    vec4 s11 = simLoad(b + ivec2(1, 1));\n"
            << "    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);\n";
    }
    out << "}\n";
}

void appendPreamble(std::ostringstream& out, const StateEncoding& e, bool sampledInFragmentStage) {
    const auto& info = imageFormatInfo(e.format);
    out << "precision highp float;\nprecision highp int;\n\n"
        << "uniform " << info.samplerType << " " << kUniformState << ";\n"
        << "uniform ivec2 " << kUniformSize << ";\n"
        << "const float SIM_STATE_SCALE = " << e.stateScale << ";\n\n"
        << "vec2 simUv(ivec2 texel) {\n    return (vec2(texel) + 0.5) / vec2(" << kUniformSize << ");\n}\n\n"
        << "vec4 simLoad(ivec2 texel) {\n"
        << "    ivec2 at = clamp(texel, ivec2(0), " << kUniformSize << " - ivec2(1));\n"
        << "    return " << loadExpression(e, std::string("texelFetch(") + kUniformState + ", at, 0)") << ";\n}\n\n";
    appendSampler(out, e, sampledInFragmentStage);
    out << "\n";
}

const char* kStep = "simStep(texel, uSimSize, simLoad(texel))";

}  // namespace

const char* fullscreenVertex() {
    return "#version 300 es\nvoid main() {\n    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
           "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n}\n";
}

std::string fragmentStep(const StateEncoding& encoding, const std::string& body) {
    std::ostringstream out;
    out << "#version 300 es\n";
    appendPreamble(out, encoding, true);
    out << "out " << imageFormatInfo(encoding.format).texelType << " simOut;\n\n" << trim(body) << "\n\n"
        << "void main() {\n    ivec2 texel = ivec2(gl_FragCoord.xy);\n"
        << "    simOut = " << storeExpression(encoding, kStep) << ";\n}\n";
    return out.str();
}

std::string computeStep(const StateEncoding& encoding, const WorkGroupSize& localSize, const std::string& body) {
    const auto& info = imageFormatInfo(encoding.format);
    std::ostringstream out;
    out << "#version 310 es\n";
    appendPreamble(out, encoding, false);
    out << localSize.layoutQualifier() << "\n\n"
        << "layout(" << info.layoutQualifier << ", binding = " << kStateImageUnit << ") writeonly uniform " << info.imageType << " simOut;\n\n"
        << trim(body) << "\n\n"
        << "void main() {\n    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);\n"
        << "    if (any(greaterThanEqual(texel, " << kUniformSize << "))) return;\n"
        << "    imageStore(simOut, texel, " << storeExpression(encoding, kStep) << ");\n}\n";
    return out.str();
}

std::string displayShader(const StateEncoding& encoding, const std::string& body) {
    std::ostringstream out;
    out << "#version 300 es\n";
    appendPreamble(out, encoding, true);
    out << trim(body) << "\n";
    return out.str();
}

}  // namespace geode::viz::sim::glsl
