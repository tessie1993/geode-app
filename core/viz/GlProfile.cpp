#include "viz/GlProfile.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

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

// A throwaway 1x1 pbuffer context; the display is never terminated because it is shared with the app's surface.
class ProbeContext {
public:
    ProbeContext() {
        if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
            failure_ = "EGL already_current: a GL context is already current on this thread; probe it directly instead";
            return;
        }
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY) { fail("display", "no default display"); return; }
        EGLint major = 0, minor = 0;
        if (!eglInitialize(display_, &major, &minor)) { fail("initialize", "eglInitialize refused the default display"); return; }
        const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!eglChooseConfig(display_, configAttribs, &config, 1, &count) || count <= 0) { fail("config", "no ES3 pbuffer config"); return; }
        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
        if (context_ == EGL_NO_CONTEXT) { fail("context", "eglCreateContext failed"); return; }
        const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, pbufferAttribs);
        if (surface_ == EGL_NO_SURFACE) { fail("surface", "eglCreatePbufferSurface failed"); return; }
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) { fail("make_current", "eglMakeCurrent failed"); return; }
        madeCurrent_ = true;
    }

    ~ProbeContext() {
        if (display_ == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (madeCurrent_) eglReleaseThread();
    }

    bool current() const { return madeCurrent_; }
    const std::string& failure() const { return failure_; }

private:
    void fail(const char* stage, const char* detail) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, "EGL %s: %s (eglGetError=0x%x)", stage, detail, eglGetError());
        failure_ = buffer;
        GEODE_LOGW(kTag, "probe context unavailable: %s", buffer);
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    bool madeCurrent_ = false;
    std::string failure_;
};

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

GlProfile DeviceGl::profileInOwnContext() {
    ProbeContext context;
    if (!context.current()) {
        GEODE_LOGW(kTag, "no probe context (%s); staying on the ES 3.0 baseline", context.failure().c_str());
        return GlProfile::unprobed(context.failure());
    }
    return profileWithCurrentContext();
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
