#pragma once
#include <GLES3/gl31.h>

#include <optional>
#include <string>

#include "viz/GlProfile.hpp"

namespace geode::viz {

struct WorkGroupCount {
    int x = 1;
    int y = 1;
    int z = 1;
    static constexpr WorkGroupCount specFloor() { return {65535, 65535, 65535}; }
};

struct WorkGroupSize {
    int x = 1;
    int y = 1;
    int z = 1;

    static constexpr int kTargetInvocations = 64;
    static constexpr WorkGroupSize specFloor() { return {128, 128, 64}; }

    int invocations() const { return x * y * z; }
    std::string layoutQualifier() const;
    WorkGroupCount groupsFor(int width, int height) const;
    std::string toString() const;
    bool operator==(const WorkGroupSize& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const WorkGroupSize& o) const { return !(*this == o); }
};

struct ComputeLimits {
    int maxInvocationsPerGroup = 0;
    WorkGroupSize maxGroupSize;
    WorkGroupCount maxGroupCount;
    int sharedMemoryBytes = 0;
    int imageUniforms = 0;
    int textureImageUnits = 0;
    bool storageBuffers = false;

    static constexpr int kSpecFloorSharedMemoryBytes = 16384;
    bool sharedMemoryMeetsSpecFloor() const { return sharedMemoryBytes >= kSpecFloorSharedMemoryBytes; }
    std::string summary() const;
};

WorkGroupSize workGroupSizeForGrid(const ComputeLimits& limits, int preferredInvocations = WorkGroupSize::kTargetInvocations);

enum class NoCompute { DeviceIsBaseline, GroupSizeBelowSpecFloor, GroupCountBelowSpecFloor, LimitsUnreadable };

struct ComputeSupport {
    bool available = false;
    ComputeLimits limits;
    ComputeProof proof;
    NoCompute cause = NoCompute::DeviceIsBaseline;
    std::string because;

    // Needs a current context when the profile says compute; touches nothing on the baseline.
    static ComputeSupport query(const GlProfile& profile);
};

enum class ImageAccess { Read = GL_READ_ONLY, Write = GL_WRITE_ONLY, ReadWrite = GL_READ_WRITE };

enum class GlImageFormat { RGBA32UI, RGBA16F, RGBA8, R32F };

struct GlImageFormatInfo {
    GLenum internalFormat;
    const char* layoutQualifier;
    const char* samplerType;
    const char* imageType;
    const char* texelType;
    bool integerTexels;
};

const GlImageFormatInfo& imageFormatInfo(GlImageFormat format);
std::optional<GlImageFormat> imageFormatOf(ProbedFormat format);

class ComputeProgram {
public:
    // Compiles and links a compute shader; the linked local size must match `expected`.
    static std::optional<ComputeProgram> build(const std::string& label, const std::string& source, WorkGroupSize expected,
                                               std::string* error);

    GLuint program() const { return program_; }
    const WorkGroupSize& localSize() const { return localSize_; }
    void use() const { glUseProgram(program_); }
    GLint uniformLocation(const char* name) const { return glGetUniformLocation(program_, name); }
    void release();
    void forget() { released_ = true; }

private:
    ComputeProgram(GLuint program, WorkGroupSize localSize) : program_(program), localSize_(localSize) {}

    GLuint program_;
    WorkGroupSize localSize_;
    bool released_ = false;
};

enum class ComputeReader : GLbitfield {
    TextureSample = GL_TEXTURE_FETCH_BARRIER_BIT,
    ImageLoadStore = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT,
    ShaderStorage = GL_SHADER_STORAGE_BARRIER_BIT,
    VertexAttributes = GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT,
    ElementIndices = GL_ELEMENT_ARRAY_BARRIER_BIT,
    UniformBlock = GL_UNIFORM_BARRIER_BIT,
    IndirectCommand = GL_COMMAND_BARRIER_BIT,
    FramebufferAttachment = GL_FRAMEBUFFER_BARRIER_BIT,
    TextureUpdate = GL_TEXTURE_UPDATE_BARRIER_BIT,
    BufferUpdate = GL_BUFFER_UPDATE_BARRIER_BIT,
    PixelTransfer = GL_PIXEL_BUFFER_BARRIER_BIT,
    AtomicCounter = GL_ATOMIC_COUNTER_BARRIER_BIT,
    TransformFeedback = GL_TRANSFORM_FEEDBACK_BARRIER_BIT,
};

class ComputePass {
public:
    ComputePass(std::string label, ComputeProgram program, GLbitfield barrierMask);

    const WorkGroupSize& localSize() const { return program_.localSize(); }
    GLuint programName() const { return program_.program(); }
    void begin() const { program_.use(); }
    GLint uniformLocation(const char* name) const { return program_.uniformLocation(name); }
    void image(int unit, GLuint texture, GlImageFormat format, ImageAccess access);
    void texture(int unit, GLuint texture);
    void storageBuffer(int index, GLuint buffer);
    void dispatch(WorkGroupCount groups);
    void end();
    void release() { program_.release(); }
    void forget() { program_.forget(); }

private:
    static constexpr int kTrackedUnits = 32;
    static uint32_t bit(int unit) { return (unit >= 0 && unit < kTrackedUnits) ? (1u << unit) : 0u; }

    std::string label_;
    ComputeProgram program_;
    GLbitfield barrierMask_;
    uint32_t imageUnits_ = 0;
    uint32_t textureUnits_ = 0;
    uint32_t storageBindings_ = 0;
};

}  // namespace geode::viz
