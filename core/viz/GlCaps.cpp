#include "viz/GlCaps.hpp"

#include <array>
#include <cctype>
#include <vector>
#include <sstream>

namespace geode::viz {

namespace {

constexpr GlVersion kEs31{3, 1};
constexpr int kMinComputeInvocations = 128;
constexpr int kMinComputeStorageBlocks = 4;
constexpr int kMinComputeImageUniforms = 4;
constexpr std::array<const char*, kProbedFormatCount> kFormatNames = {"RGBA8", "R16F", "RG16F", "RGBA16F", "R32F", "RGBA32UI"};

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

ResolvedFormat lastResort(const std::string& why) {
    return {ProbedFormat::RGBA8, TexelEncoding::PreScaled, true, why + "; RGBA8 is the core-mandated floor"};
}

std::string flags(const FormatProbe& p) {
    std::string out;
    for (const bool b : {p.attachable, p.rendersExactly, p.blendsAdditively, p.filtersLinearly}) out += b ? '1' : '0';
    return out;
}

std::optional<FormatProbe> unflags(const std::string& text) {
    if (text.size() != 4) return std::nullopt;
    for (const char c : text) {
        if (c != '0' && c != '1') return std::nullopt;
    }
    return FormatProbe{text[0] == '1', text[1] == '1', text[2] == '1', text[3] == '1'};
}

class LineReader {
public:
    explicit LineReader(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines_.push_back(line);
        }
    }
    bool exhausted() const { return index_ >= lines_.size(); }
    std::optional<std::string> next() {
        if (exhausted()) return std::nullopt;
        return lines_[index_++];
    }
    std::optional<std::string> value(const std::string& key) {
        const auto line = next();
        if (!line || !startsWith(*line, key + "=")) return std::nullopt;
        return line->substr(key.size() + 1);
    }
    std::optional<int> intValue(const std::string& key) {
        const auto v = value(key);
        if (!v) return std::nullopt;
        try {
            size_t used = 0;
            const int parsed = std::stoi(*v, &used);
            if (used != v->size()) return std::nullopt;
            return parsed;
        } catch (...) {
            return std::nullopt;
        }
    }
    std::optional<bool> boolValue(const std::string& key) {
        const auto v = value(key);
        if (!v) return std::nullopt;
        if (*v == "true") return true;
        if (*v == "false") return false;
        return std::nullopt;
    }

private:
    std::vector<std::string> lines_;
    size_t index_ = 0;
};

}  // namespace

std::optional<GlVersion> GlVersion::parse(const std::string& versionString) {
    // Matches ^OpenGL ES(?:-C[ML])? (\d+)\.(\d+)
    static const std::string kPrefix = "OpenGL ES";
    if (!startsWith(versionString, kPrefix)) return std::nullopt;
    size_t i = kPrefix.size();
    if (versionString.compare(i, 3, "-CM") == 0 || versionString.compare(i, 3, "-CL") == 0) i += 3;
    if (i >= versionString.size() || versionString[i] != ' ') return std::nullopt;
    i++;
    auto digits = [&](int& out) {
        const size_t start = i;
        while (i < versionString.size() && std::isdigit(static_cast<unsigned char>(versionString[i]))) i++;
        if (i == start) return false;
        out = std::stoi(versionString.substr(start, i - start));
        return true;
    };
    GlVersion v;
    if (!digits(v.major)) return std::nullopt;
    if (i >= versionString.size() || versionString[i] != '.') return std::nullopt;
    i++;
    if (!digits(v.minor)) return std::nullopt;
    return v;
}

const char* probedFormatName(ProbedFormat format) { return kFormatNames[static_cast<int>(format)]; }

std::optional<ProbedFormat> probedFormatOf(const std::string& name) {
    for (int i = 0; i < kProbedFormatCount; i++) {
        if (name == kFormatNames[i]) return static_cast<ProbedFormat>(i);
    }
    return std::nullopt;
}

FormatProbe GlProbeReport::probeOf(ProbedFormat format) const {
    const auto it = formats.find(format);
    return it == formats.end() ? FormatProbe{} : it->second;
}

GlProbeReport GlProbeReport::unprobed(const GlIdentity& identity) {
    GlProbeReport report;
    report.vendor = identity.vendor;
    report.renderer = identity.renderer;
    report.versionString = identity.versionString;
    return report;
}

const char* timerQuerySupportName(TimerQuerySupport support) {
    switch (support) {
        case TimerQuerySupport::Absent: return "ABSENT";
        case TimerQuerySupport::Untrusted: return "UNTRUSTED";
        case TimerQuerySupport::Trusted: return "TRUSTED";
    }
    return "ABSENT";
}

GlCapabilities GlCapabilities::derive(const GlProbeReport& report) {
    GlCapabilities c;
    c.version = GlVersion::parse(report.versionString);
    const bool es31 = c.version && *c.version >= kEs31;
    c.computeShaders = es31 && report.maxComputeWorkGroupInvocations >= kMinComputeInvocations;
    c.storageBuffersInCompute = es31 && report.maxComputeStorageBlocks >= kMinComputeStorageBlocks;
    c.storageBuffersInFragment = es31 && report.maxFragmentStorageBlocks > 0;
    c.imageLoadStore = es31 && report.maxComputeImageUniforms >= kMinComputeImageUniforms;
    c.vertexTextureFetch = report.maxVertexTextureImageUnits > 0 && report.vertexTextureFetchProven;
    c.timerQueries = !report.timerQueryPresent ? TimerQuerySupport::Absent
                     : !report.timerQueryProven ? TimerQuerySupport::Untrusted
                                                 : TimerQuerySupport::Trusted;
    c.programBinaries = report.programBinaryFormats > 0;
    return c;
}

std::string ComputeProof::sentence() const {
    return "ES " + version.toString() + " with " + std::to_string(workGroupInvocations) +
           " compute invocations per work group and " + std::to_string(computeImageUniforms) + " image uniforms" +
           (storageBuffersInCompute ? ", plus compute SSBOs" : ", but no compute SSBOs") +
           "; a simulation step can scatter without emitting a primitive";
}

GlTier GlTier::baseline(BaselineCause cause, const std::string& detail) {
    GlTier t;
    t.compute = false;
    t.cause = cause;
    switch (cause) {
        case BaselineCause::NoProbeContext:
            t.because = "no GL context could be probed (" + detail + "); the ES 3.0 baseline is the one claim that needs no evidence";
            break;
        case BaselineCause::VersionUnparseable:
            t.because = "GL_VERSION did not parse (\"" + detail + "\"); an unreadable version enables nothing enhanced";
            break;
        case BaselineCause::BelowEs31:
            t.because = "ES " + detail + " has no compute shaders; fragment ping-pong is the correct path here, not a fallback";
            break;
        case BaselineCause::ComputeLimitsBelowSpecFloor:
            t.because = "the context claims ES 3.1 but reports " + detail +
                        " compute invocations per work group, below the spec floor of 128; a version string that undershoots its own minimum is not evidence";
            break;
        case BaselineCause::NoImageLoadStore:
            t.because = "compute is present but only " + detail +
                        " compute image uniforms are, so a step cannot scatter into an image; compute without scatter buys nothing the fragment path lacks";
            break;
    }
    return t;
}

GlTier GlTier::of(const GlProbeReport& report, const GlCapabilities& capabilities) {
    if (!capabilities.version) return baseline(BaselineCause::VersionUnparseable, report.versionString);
    const GlVersion version = *capabilities.version;
    if (version < kEs31) return baseline(BaselineCause::BelowEs31, version.toString());
    if (!capabilities.computeShaders) {
        return baseline(BaselineCause::ComputeLimitsBelowSpecFloor, std::to_string(report.maxComputeWorkGroupInvocations));
    }
    if (!capabilities.imageLoadStore) {
        return baseline(BaselineCause::NoImageLoadStore, std::to_string(report.maxComputeImageUniforms));
    }
    GlTier t;
    t.compute = true;
    t.proof = ComputeProof{version, report.maxComputeWorkGroupInvocations, report.maxComputeImageUniforms,
                           capabilities.storageBuffersInCompute};
    t.because = t.proof.sentence();
    return t;
}

FormatPlan FormatPlan::resolve(const GlProbeReport& report) {
    FormatPlan plan;
    {
        const FormatProbe state = report.probeOf(ProbedFormat::RGBA32UI);
        const FormatProbe half = report.probeOf(ProbedFormat::RGBA16F);
        if (state.renderable()) {
            plan.simulationState = {ProbedFormat::RGBA32UI, TexelEncoding::FloatBitsInUint, false,
                                    "RGBA32UI proved renderable; float bits packed into uint channels"};
        } else if (half.renderable()) {
            plan.simulationState = {ProbedFormat::RGBA16F, TexelEncoding::Linear, half.filtersLinearly,
                                    "RGBA32UI failed its probe on this driver; RGBA16F proved renderable"};
        } else {
            plan.simulationState = lastResort("neither RGBA32UI nor RGBA16F proved renderable");
        }
    }
    {
        const FormatProbe half = report.probeOf(ProbedFormat::RGBA16F);
        plan.advectedField = half.renderable()
            ? ResolvedFormat{ProbedFormat::RGBA16F, TexelEncoding::Linear, half.filtersLinearly,
                             "RGBA16F proved renderable; the dye keeps its headroom above 1 in half floats"}
            : ResolvedFormat{ProbedFormat::RGBA8, TexelEncoding::PreScaled, true,
                             "RGBA16F is not renderable here; pre-scaled RGBA8 keeps the headroom at 8 bits of resolution"};
    }
    {
        const FormatProbe rg = report.probeOf(ProbedFormat::RG16F);
        const FormatProbe state = report.probeOf(ProbedFormat::RGBA32UI);
        if (rg.renderable() && rg.filtersLinearly) {
            plan.filterableField = {ProbedFormat::RG16F, TexelEncoding::Linear, true, "RG16F proved renderable and filterable"};
        } else if (state.renderable()) {
            plan.filterableField = {ProbedFormat::RGBA32UI, TexelEncoding::FloatBitsInUint, false,
                                    "RG16F unproven; packed state with manual interpolation in the shader"};
        } else {
            plan.filterableField = lastResort("neither RG16F nor RGBA32UI proved renderable");
        }
    }
    {
        const FormatProbe r16f = report.probeOf(ProbedFormat::R16F);
        plan.linearAccumulation = (r16f.renderable() && r16f.blendsAdditively)
            ? ResolvedFormat{ProbedFormat::R16F, TexelEncoding::Linear, r16f.filtersLinearly,
                             "R16F proved renderable and additively blendable"}
            : ResolvedFormat{ProbedFormat::RGBA8, TexelEncoding::PreScaled, true,
                             "R16F failed the render-and-blend probe; pre-scaled RGBA8 keeps deposits linear"};
        plan.audioTexture = r16f.filtersLinearly
            ? ResolvedFormat{ProbedFormat::R16F, TexelEncoding::Linear, true,
                             "R16F proved filterable; audio textures are uploaded, so renderability is not required"}
            : ResolvedFormat{ProbedFormat::RGBA8, TexelEncoding::PreScaled, true,
                             "R16F failed the filter probe despite being core; pre-scaled RGBA8 fallback"};
    }
    {
        const FormatProbe half = report.probeOf(ProbedFormat::RGBA16F);
        plan.linearColorTarget = half.renderable()
            ? ResolvedFormat{ProbedFormat::RGBA16F, TexelEncoding::Linear, half.filtersLinearly,
                             "RGBA16F proved renderable; linear-light headroom available"}
            : ResolvedFormat{ProbedFormat::RGBA8, TexelEncoding::Linear, true,
                             "RGBA16F unproven; RGBA8 target with range clamped at 1.0"};
    }
    return plan;
}

namespace capability_cache {

namespace {
constexpr const char* kHeader = "geode-gl-probe-cache";
std::string boolText(bool b) { return b ? "true" : "false"; }
}  // namespace

std::string encode(const GlProbeReport& r) {
    std::string out;
    out += std::string(kHeader) + " v" + std::to_string(kSchemaVersion) + "\n";
    out += "vendor=" + r.vendor + "\n";
    out += "renderer=" + r.renderer + "\n";
    out += "version=" + r.versionString + "\n";
    out += "extensions=";
    bool first = true;
    for (const auto& e : r.extensions) {
        if (!first) out += ' ';
        out += e;
        first = false;
    }
    out += "\n";
    out += "maxTextureSize=" + std::to_string(r.maxTextureSize) + "\n";
    out += "maxColorAttachments=" + std::to_string(r.maxColorAttachments) + "\n";
    out += "maxVertexTextureImageUnits=" + std::to_string(r.maxVertexTextureImageUnits) + "\n";
    out += "vertexTextureFetchProven=" + boolText(r.vertexTextureFetchProven) + "\n";
    out += "maxComputeWorkGroupInvocations=" + std::to_string(r.maxComputeWorkGroupInvocations) + "\n";
    out += "maxComputeStorageBlocks=" + std::to_string(r.maxComputeStorageBlocks) + "\n";
    out += "maxFragmentStorageBlocks=" + std::to_string(r.maxFragmentStorageBlocks) + "\n";
    out += "maxComputeImageUniforms=" + std::to_string(r.maxComputeImageUniforms) + "\n";
    out += "programBinaryFormats=" + std::to_string(r.programBinaryFormats) + "\n";
    out += "timerQueryPresent=" + boolText(r.timerQueryPresent) + "\n";
    out += "timerQueryProven=" + boolText(r.timerQueryProven) + "\n";
    for (int i = 0; i < kProbedFormatCount; i++) {
        const auto format = static_cast<ProbedFormat>(i);
        const auto it = r.formats.find(format);
        if (it != r.formats.end()) out += std::string("format.") + probedFormatName(format) + "=" + flags(it->second) + "\n";
    }
    return out;
}

std::optional<GlProbeReport> decode(const std::string& text, const std::string& vendor, const std::string& renderer,
                                    const std::string& versionString) {
    LineReader reader(text);
    if (reader.next() != std::optional<std::string>(std::string(kHeader) + " v" + std::to_string(kSchemaVersion))) return std::nullopt;
    const auto cachedVendor = reader.value("vendor");
    const auto cachedRenderer = reader.value("renderer");
    const auto cachedVersion = reader.value("version");
    if (!cachedVendor || !cachedRenderer || !cachedVersion) return std::nullopt;
    if (*cachedVendor != vendor || *cachedRenderer != renderer || *cachedVersion != versionString) return std::nullopt;

    const auto extensionsLine = reader.value("extensions");
    if (!extensionsLine) return std::nullopt;
    GlProbeReport r;
    r.vendor = *cachedVendor;
    r.renderer = *cachedRenderer;
    r.versionString = *cachedVersion;
    {
        std::istringstream in(*extensionsLine);
        std::string e;
        while (in >> e) r.extensions.insert(e);
    }
    auto intField = [&](const char* key, int& out) {
        const auto v = reader.intValue(key);
        if (!v) return false;
        out = *v;
        return true;
    };
    auto boolField = [&](const char* key, bool& out) {
        const auto v = reader.boolValue(key);
        if (!v) return false;
        out = *v;
        return true;
    };
    if (!intField("maxTextureSize", r.maxTextureSize) || !intField("maxColorAttachments", r.maxColorAttachments) ||
        !intField("maxVertexTextureImageUnits", r.maxVertexTextureImageUnits) ||
        !boolField("vertexTextureFetchProven", r.vertexTextureFetchProven) ||
        !intField("maxComputeWorkGroupInvocations", r.maxComputeWorkGroupInvocations) ||
        !intField("maxComputeStorageBlocks", r.maxComputeStorageBlocks) ||
        !intField("maxFragmentStorageBlocks", r.maxFragmentStorageBlocks) ||
        !intField("maxComputeImageUniforms", r.maxComputeImageUniforms) ||
        !intField("programBinaryFormats", r.programBinaryFormats) || !boolField("timerQueryPresent", r.timerQueryPresent) ||
        !boolField("timerQueryProven", r.timerQueryProven)) {
        return std::nullopt;
    }
    while (!reader.exhausted()) {
        const auto line = reader.next();
        if (!line) return std::nullopt;
        const size_t eq = line->find('=');
        if (!startsWith(*line, "format.") || eq == std::string::npos) return std::nullopt;
        const auto format = probedFormatOf(line->substr(7, eq - 7));
        const auto probe = unflags(line->substr(eq + 1));
        if (!format || !probe) return std::nullopt;
        if (!r.formats.emplace(*format, *probe).second) return std::nullopt;
    }
    return r;
}

}  // namespace capability_cache

}  // namespace geode::viz
