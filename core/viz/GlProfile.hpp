#pragma once
#include <optional>
#include <string>

#include "viz/GlCaps.hpp"

namespace geode::viz {

enum class ProbeSource { CachedFacts, FreshProbe, NoContext };

struct GlProfile {
    GlProbeReport report;
    GlCapabilities capabilities;
    FormatPlan formats;
    GlTier tier;
    ProbeSource source = ProbeSource::NoContext;

    std::string summary() const;
    static GlProfile unprobed(const std::string& detail = "not probed yet");
};

// Port of DeviceGl: cache the facts in <cacheDir>/gl-probe-facts.txt, derive the judgments fresh.
class DeviceGl {
public:
    explicit DeviceGl(std::string cacheDir) : cacheDir_(std::move(cacheDir)) {}

    // Requires a current context on the calling thread.
    GlProfile profileWithCurrentContext();
    // Requires NO current context: makes a 1x1 pbuffer context and releases it.
    GlProfile profileInOwnContext();
    void forget();

private:
    static GlProfile profileOf(const GlProbeReport& report, ProbeSource source);
    std::string cacheFile() const;
    std::optional<GlProbeReport> readCache(const GlIdentity& identity) const;
    void writeCache(const GlProbeReport& report) const;
    GlProfile remember(GlProfile profile);

    std::string cacheDir_;
    std::optional<GlProfile> memo_;
};

}  // namespace geode::viz
