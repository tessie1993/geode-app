#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>

namespace geode::viz {

struct GlVersion {
    int major = 0;
    int minor = 0;

    static std::optional<GlVersion> parse(const std::string& versionString);
    bool operator<(const GlVersion& o) const { return major != o.major ? major < o.major : minor < o.minor; }
    bool operator>=(const GlVersion& o) const { return !(*this < o); }
    std::string toString() const { return std::to_string(major) + "." + std::to_string(minor); }
};

enum class ProbedFormat { RGBA8, R16F, RG16F, RGBA16F, R32F, RGBA32UI };
constexpr int kProbedFormatCount = 6;
const char* probedFormatName(ProbedFormat format);
std::optional<ProbedFormat> probedFormatOf(const std::string& name);

struct FormatProbe {
    bool attachable = false;
    bool rendersExactly = false;
    bool blendsAdditively = false;
    bool filtersLinearly = false;
    bool renderable() const { return attachable && rendersExactly; }
};

struct GlIdentity {
    std::string vendor;
    std::string renderer;
    std::string versionString;
};

struct GlProbeReport {
    std::string vendor;
    std::string renderer;
    std::string versionString;
    std::set<std::string> extensions;
    int maxTextureSize = 0;
    int maxColorAttachments = 0;
    int maxVertexTextureImageUnits = 0;
    bool vertexTextureFetchProven = false;
    int maxComputeWorkGroupInvocations = 0;
    int maxComputeStorageBlocks = 0;
    int maxFragmentStorageBlocks = 0;
    int maxComputeImageUniforms = 0;
    int programBinaryFormats = 0;
    bool timerQueryPresent = false;
    bool timerQueryProven = false;
    std::map<ProbedFormat, FormatProbe> formats;

    FormatProbe probeOf(ProbedFormat format) const;
    static GlProbeReport unprobed(const GlIdentity& identity = {});
};

enum class TimerQuerySupport { Absent, Untrusted, Trusted };
const char* timerQuerySupportName(TimerQuerySupport support);

struct GlCapabilities {
    std::optional<GlVersion> version;
    bool computeShaders = false;
    bool storageBuffersInCompute = false;
    bool storageBuffersInFragment = false;
    bool imageLoadStore = false;
    bool vertexTextureFetch = false;
    TimerQuerySupport timerQueries = TimerQuerySupport::Absent;
    bool programBinaries = false;

    static GlCapabilities derive(const GlProbeReport& report);
};

enum class BaselineCause { NoProbeContext, VersionUnparseable, BelowEs31, ComputeLimitsBelowSpecFloor, NoImageLoadStore };

struct ComputeProof {
    GlVersion version;
    int workGroupInvocations = 0;
    int computeImageUniforms = 0;
    bool storageBuffersInCompute = false;
    std::string sentence() const;
};

struct GlTier {
    bool compute = false;
    BaselineCause cause = BaselineCause::NoProbeContext;
    ComputeProof proof;
    std::string because;

    const char* label() const { return compute ? "compute-es31" : "baseline-es30"; }
    static GlTier baseline(BaselineCause cause, const std::string& detail);
    static GlTier of(const GlProbeReport& report, const GlCapabilities& capabilities);
};

enum class TexelEncoding { Linear, FloatBitsInUint, PreScaled };

struct ResolvedFormat {
    ProbedFormat format = ProbedFormat::RGBA8;
    TexelEncoding encoding = TexelEncoding::PreScaled;
    bool filterable = true;
    std::string because;
};

struct FormatPlan {
    ResolvedFormat simulationState;
    ResolvedFormat advectedField;
    ResolvedFormat filterableField;
    ResolvedFormat linearAccumulation;
    ResolvedFormat audioTexture;
    ResolvedFormat linearColorTarget;

    static FormatPlan resolve(const GlProbeReport& report);
};

namespace capability_cache {
constexpr int kSchemaVersion = 1;
std::string encode(const GlProbeReport& report);
std::optional<GlProbeReport> decode(const std::string& text, const std::string& vendor, const std::string& renderer,
                                    const std::string& versionString);
}  // namespace capability_cache

}  // namespace geode::viz
