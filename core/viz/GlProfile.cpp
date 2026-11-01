#include "viz/GlProfile.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "util/Log.hpp"
#include "viz/GlProber.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "DeviceGl";
constexpr const char* kCacheFile = "gl-probe-facts.txt";

const char* sourceName(ProbeSource s) {
    switch (s) {
        case ProbeSource::CachedFacts: return "CACHED_FACTS";
        case ProbeSource::FreshProbe: return "FRESH_PROBE";
        case ProbeSource::NoContext: return "NO_CONTEXT";
    }
    return "NO_CONTEXT";
}

}  // namespace

std::string GlProfile::summary() const {
    std::ostringstream out;
    out << "gl=" << tier.label() << " source=" << sourceName(source)
        << " state=" << probedFormatName(formats.simulationState.format)
        << " accum=" << probedFormatName(formats.linearAccumulation.format)
        << " colour=" << probedFormatName(formats.linearColorTarget.format)
        << " vtf=" << (capabilities.vertexTextureFetch ? "true" : "false")
        << " timer=" << timerQuerySupportName(capabilities.timerQueries) << " — " << tier.because;
    return out.str();
}

GlProfile GlProfile::unprobed(const std::string& detail) {
    GlProfile p;
    p.report = GlProbeReport::unprobed();
    p.capabilities = GlCapabilities::derive(p.report);
    p.formats = FormatPlan::resolve(p.report);
    p.tier = GlTier::baseline(BaselineCause::NoProbeContext, detail);
    p.source = ProbeSource::NoContext;
    return p;
}

GlProfile DeviceGl::profileWithCurrentContext() {
    const GlIdentity identity = prober::identity();
    if (memo_ && memo_->report.vendor == identity.vendor && memo_->report.renderer == identity.renderer &&
        memo_->report.versionString == identity.versionString) {
        return *memo_;
    }
    if (const auto cached = readCache(identity)) return remember(profileOf(*cached, ProbeSource::CachedFacts));
    const GlProbeReport report = prober::probe();
    writeCache(report);
    return remember(profileOf(report, ProbeSource::FreshProbe));
}

void DeviceGl::forget() {
    memo_.reset();
    std::remove(cacheFile().c_str());
}

GlProfile DeviceGl::profileOf(const GlProbeReport& report, ProbeSource source) {
    GlProfile p;
    p.report = report;
    p.capabilities = GlCapabilities::derive(report);
    p.formats = FormatPlan::resolve(report);
    p.tier = GlTier::of(report, p.capabilities);
    p.source = source;
    return p;
}

std::string DeviceGl::cacheFile() const { return cacheDir_ + "/" + kCacheFile; }

std::optional<GlProbeReport> DeviceGl::readCache(const GlIdentity& identity) const {
    std::ifstream in(cacheFile());
    if (!in) return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    return capability_cache::decode(text.str(), identity.vendor, identity.renderer, identity.versionString);
}

void DeviceGl::writeCache(const GlProbeReport& report) const {
    if (report.versionString.empty()) return;
    std::ofstream out(cacheFile(), std::ios::trunc);
    if (!out) {
        GEODE_LOGW(kTag, "write the GL probe cache failed");
        return;
    }
    out << capability_cache::encode(report);
}

GlProfile DeviceGl::remember(GlProfile profile) {
    memo_ = profile;
    GEODE_LOGI(kTag, "%s", profile.summary().c_str());
    return profile;
}

}  // namespace geode::viz
