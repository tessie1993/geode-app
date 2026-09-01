#include "viz/Compute.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include "util/Log.hpp"
#include "viz/GlProber.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "ComputeSupport";
constexpr const char* kProgramTag = "ComputeProgram";
constexpr const char* kPassTag = "ComputePass";

int ceilDiv(int value, int divisor) { return (value + divisor - 1) / divisor; }

int largestPowerOfTwoAtMost(int value) {
    int result = 1;
    while (result * 2 <= value) result *= 2;
    return result;
}

int indexedLimit(GLenum pname, GLuint index) {
    prober::drainErrors();
    GLint out = 0;
    glGetIntegeri_v(pname, index, &out);
    return glGetError() != GL_NO_ERROR ? 0 : out;
}

std::string numbered(const std::string& source) {
    std::ostringstream out;
    std::istringstream in(source);
    std::string line;
    int index = 1;
    while (std::getline(in, line)) out << index++ << ": " << line << "\n";
    return out.str();
}

}  // namespace

std::string WorkGroupSize::layoutQualifier() const {
    return "layout(local_size_x = " + std::to_string(x) + ", local_size_y = " + std::to_string(y) +
           ", local_size_z = " + std::to_string(z) + ") in;";
}

WorkGroupCount WorkGroupSize::groupsFor(int width, int height) const {
    return WorkGroupCount{ceilDiv(width, x), ceilDiv(height, y), 1};
}

std::string WorkGroupSize::toString() const {
    return std::to_string(x) + "x" + std::to_string(y) + "x" + std::to_string(z);
}

std::string ComputeLimits::summary() const {
    std::ostringstream out;
    out << "invocations=" << maxInvocationsPerGroup << " groupSize=" << maxGroupSize.toString() << " groupCount="
        << maxGroupCount.x << "x" << maxGroupCount.y << "x" << maxGroupCount.z << " shared=" << sharedMemoryBytes << "B"
        << (sharedMemoryMeetsSpecFloor() ? "" : " (BELOW SPEC FLOOR)") << " images=" << imageUniforms
        << " textures=" << textureImageUnits << " ssbo=" << (storageBuffers ? "true" : "false");
    return out.str();
}

WorkGroupSize workGroupSizeForGrid(const ComputeLimits& limits, int preferredInvocations) {
    const int budget = largestPowerOfTwoAtMost(std::clamp(preferredInvocations, 1, std::max(limits.maxInvocationsPerGroup, 1)));
    int x = 1;
    int y = 1;
    while (x * y * 2 <= budget) {
        const bool growX = x <= y && x * 2 <= limits.maxGroupSize.x;
        if (growX) {
            x *= 2;
        } else if (y * 2 <= limits.maxGroupSize.y) {
            y *= 2;
        } else if (x * 2 <= limits.maxGroupSize.x) {
            x *= 2;
        } else {
            break;
        }
    }
    return WorkGroupSize{x, y, 1};
}

ComputeSupport ComputeSupport::query(const GlProfile& profile) {
    ComputeSupport s;
    if (!profile.tier.compute) {
        s.cause = NoCompute::DeviceIsBaseline;
        s.because = profile.tier.because;
        return s;
    }
    const int maxInvocations = prober::limit(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    const WorkGroupSize groupSize{indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0), indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1),
                                  indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2)};
    const WorkGroupCount groupCount{indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0), indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1),
                                    indexedLimit(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2)};
    if (maxInvocations <= 0) {
        s.cause = NoCompute::LimitsUnreadable;
        s.because = "the ES 3.1 compute limits could not be read (GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS read as 0); nothing enhanced is enabled on unread limits";
        return s;
    }
    const WorkGroupSize sizeFloor = WorkGroupSize::specFloor();
    if (groupSize.x < sizeFloor.x || groupSize.y < sizeFloor.y || groupSize.z < sizeFloor.z) {
        s.cause = NoCompute::GroupSizeBelowSpecFloor;
        s.because = "the context claims ES 3.1 but its maximum work group size is " + groupSize.toString() + ", below the spec floor of " +
                    sizeFloor.toString() + "; a version string that undershoots its own minimum is not evidence";
        return s;
    }
    const WorkGroupCount countFloor = WorkGroupCount::specFloor();
    if (groupCount.x < countFloor.x || groupCount.y < countFloor.y || groupCount.z < countFloor.z) {
        s.cause = NoCompute::GroupCountBelowSpecFloor;
        s.because = "the context claims ES 3.1 but allows only " + std::to_string(groupCount.x) + "x" + std::to_string(groupCount.y) + "x" +
                    std::to_string(groupCount.z) + " work groups per dispatch, below the spec floor of " + std::to_string(countFloor.x) + "x" +
                    std::to_string(countFloor.y) + "x" + std::to_string(countFloor.z);
        return s;
    }
    s.available = true;
    s.limits = ComputeLimits{maxInvocations, groupSize, groupCount, prober::limit(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE),
                             profile.report.maxComputeImageUniforms, prober::limit(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS),
                             profile.capabilities.storageBuffersInCompute};
    s.proof = profile.tier.proof;
    s.because = s.proof.sentence() + "; " + s.limits.summary();
    GEODE_LOGI(kTag, "%s", s.because.c_str());
    return s;
}

const GlImageFormatInfo& imageFormatInfo(GlImageFormat format) {
    static const std::array<GlImageFormatInfo, 4> kInfos = {{
        {GL_RGBA32UI, "rgba32ui", "highp usampler2D", "highp uimage2D", "uvec4", true},
        {GL_RGBA16F, "rgba16f", "highp sampler2D", "highp image2D", "vec4", false},
        {GL_RGBA8, "rgba8", "highp sampler2D", "highp image2D", "vec4", false},
        {GL_R32F, "r32f", "highp sampler2D", "highp image2D", "vec4", false},
    }};
    return kInfos[static_cast<int>(format)];
}

std::optional<GlImageFormat> imageFormatOf(ProbedFormat format) {
    switch (format) {
        case ProbedFormat::RGBA8: return GlImageFormat::RGBA8;
        case ProbedFormat::R16F: return std::nullopt;
        case ProbedFormat::RG16F: return std::nullopt;
        case ProbedFormat::RGBA16F: return GlImageFormat::RGBA16F;
        case ProbedFormat::R32F: return GlImageFormat::R32F;
        case ProbedFormat::RGBA32UI: return GlImageFormat::RGBA32UI;
    }
    return std::nullopt;
}

std::optional<ComputeProgram> ComputeProgram::build(const std::string& label, const std::string& source, WorkGroupSize expected,
                                                    std::string* error) {
    auto fail = [&](const std::string& message) {
        GEODE_LOGW(kProgramTag, "%s", numbered(source).c_str());
        if (error) *error = message;
        return std::nullopt;
    };
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0) return fail(label + ": glCreateShader(GL_COMPUTE_SHADER) returned 0; this context does not have compute");
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == 0) {
        std::array<char, 4096> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(shader);
        return fail(label + ": compute compile failed: " + (log[0] ? log.data() : "(the driver returned no message)"));
    }
    const GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(shader);
        return fail(label + ": glCreateProgram returned 0");
    }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    glDetachShader(program, shader);
    glDeleteShader(shader);
    if (status == 0) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteProgram(program);
        return fail(label + ": compute link failed: " + (log[0] ? log.data() : "(the driver returned no message)"));
    }
    GLint linked[3] = {0, 0, 0};
    glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, linked);
    const WorkGroupSize linkedSize{linked[0], linked[1], linked[2]};
    if (linkedSize != expected) {
        glDeleteProgram(program);
        return fail(label + ": linked local size is " + linkedSize.toString() + " but the source was built for " + expected.toString() +
                    "; the layout qualifier substitution did not take");
    }
    return ComputeProgram(program, linkedSize);
}

void ComputeProgram::release() {
    if (released_) return;
    released_ = true;
    glDeleteProgram(program_);
}

ComputePass::ComputePass(std::string label, ComputeProgram program, GLbitfield barrierMask)
    : label_(std::move(label)), program_(program), barrierMask_(barrierMask) {
    if (barrierMask_ == 0) GEODE_LOGW(kPassTag, "%s declares no readers, so no memory barrier will be issued after its dispatch", label_.c_str());
}

void ComputePass::image(int unit, GLuint texture, GlImageFormat format, ImageAccess access) {
    glBindImageTexture(static_cast<GLuint>(unit), texture, 0, GL_FALSE, 0, static_cast<GLenum>(access), imageFormatInfo(format).internalFormat);
    imageUnits_ |= bit(unit);
}

void ComputePass::texture(int unit, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, texture);
    textureUnits_ |= bit(unit);
}

void ComputePass::storageBuffer(int index, GLuint buffer) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(index), buffer);
    storageBindings_ |= bit(index);
}

void ComputePass::dispatch(WorkGroupCount groups) {
    if (groups.x <= 0 || groups.y <= 0 || groups.z <= 0) {
        GEODE_LOGW(kPassTag, "%s: refusing a dispatch of %dx%dx%d work groups", label_.c_str(), groups.x, groups.y, groups.z);
        return;
    }
    glDispatchCompute(static_cast<GLuint>(groups.x), static_cast<GLuint>(groups.y), static_cast<GLuint>(groups.z));
    if (barrierMask_ != 0) glMemoryBarrier(barrierMask_);
}

void ComputePass::end() {
    for (int unit = 0; unit < kTrackedUnits; unit++) {
        if (imageUnits_ & (1u << unit)) glBindImageTexture(static_cast<GLuint>(unit), 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
        if (textureUnits_ & (1u << unit)) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (storageBindings_ & (1u << unit)) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(unit), 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    imageUnits_ = 0;
    textureUnits_ = 0;
    storageBindings_ = 0;
}

}  // namespace geode::viz
