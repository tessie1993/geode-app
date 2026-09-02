#pragma once
#include <GLES3/gl31.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include "viz/Compute.hpp"
#include "viz/GlProfile.hpp"
#include "viz/Program.hpp"
#include "viz/ProgramBinaryCache.hpp"
#include "viz/compute/SimField.hpp"
#include "viz/compute/SimGlsl.hpp"

namespace geode::viz::sim {

// Port of SimUniforms: the step's uniform setters, with sampler units handed out from unit 1.
class SimUniforms {
public:
    using BindTexture = std::function<void(int unit, GLuint texture)>;

    SimUniforms(GLuint program, BindTexture bindTexture) : cache_(program), bindTexture_(std::move(bindTexture)) {}

    void f1(const char* name, float v) { glUniform1f(cache_.loc(name), v); }
    void i1(const char* name, int v) { glUniform1i(cache_.loc(name), v); }
    void b1(const char* name, bool v) { glUniform1i(cache_.loc(name), v ? 1 : 0); }
    void f2(const char* name, float x, float y) { glUniform2f(cache_.loc(name), x, y); }
    void f3(const char* name, float x, float y, float z) { glUniform3f(cache_.loc(name), x, y, z); }
    void f4(const char* name, float x, float y, float z, float w) { glUniform4f(cache_.loc(name), x, y, z, w); }
    void i2(const char* name, int x, int y) { glUniform2i(cache_.loc(name), x, y); }
    void sampler(const char* name, GLuint texture);
    void f4Array(const char* name, const float* values, int count, int declared);
    UniformCache& cache() { return cache_; }

private:
    UniformCache cache_;
    BindTexture bindTexture_;
    std::unordered_map<std::string, int> units_;
};

enum class SimSampling { WholeTexels, BetweenTexels };

struct SimSpec {
    std::string label;
    std::string stepBody;
    SimSampling sampling = SimSampling::WholeTexels;
    float stateScale = 1.0f;
    int preferredInvocations = WorkGroupSize::kTargetInvocations;
    GLbitfield resultReadBy = static_cast<GLbitfield>(ComputeReader::TextureSample);
};

using SimUniformBinder = std::function<void(SimUniforms&)>;

// Port of SimPass: one simulation step per frame, on compute where proven and fragment ping-pong otherwise.
class SimPass {
public:
    virtual ~SimPass() = default;
    virtual const std::string& label() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual const char* pathLabel() const = 0;
    virtual GLuint stateTexture() const = 0;
    virtual void resize(int width, int height) = 0;
    virtual bool step(const SimUniformBinder& binder) = 0;
    virtual void clear() = 0;
    virtual std::string displayShader(const std::string& body) const = 0;
    virtual void bindStateFor(UniformCache& display, int unit) = 0;
    virtual void release() = 0;
    virtual void forget() = 0;

    struct Failed {
        std::string message;
    };
    using Build = std::variant<std::unique_ptr<SimPass>, Failed>;

    static Build build(const SimSpec& spec, const GlProfile& gl, ProgramBinaryCache* cache, const std::function<void(const std::string&)>& onDiagnostic);
};

}  // namespace geode::viz::sim
