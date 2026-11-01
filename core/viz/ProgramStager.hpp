#pragma once
#include <GLES3/gl3.h>

#include <optional>
#include <string>
#include <vector>

#include "viz/ProgramBinaryCache.hpp"

namespace geode::viz {

struct ShaderStage {
    GLenum type;
    std::string source;
};

// The GL half of staged scene builds (see ScenePrep.hpp for why and for the frame-level steps).
//
// While an Attempt is open on the GL thread, every program::build and ComputeProgram::build call
// on that thread comes here instead of compiling inline. A program that is already compiled is
// handed over (the caller owns it from then on, exactly as if it had built it); one that is not
// is queued and the caller gets a placeholder, an empty program object that draws nothing and that
// it deletes like any other, and the attempt is marked deferred. Outside attempts, step() starts
// queued work and collects finished work inside a BuildBudget: with
// GL_KHR_parallel_shader_compile the driver compiles in the background and step() only polls
// COMPLETION_STATUS_KHR; without it, one program compiles per frame. Programs keyed for the
// ProgramBinaryCache still load from and store to it, so a warm cache makes staging nearly free.
//
// GL thread only, like the scenes. Outside an attempt nothing changes: builds compile inline.
class ProgramStager {
public:
    explicit ProgramStager(ProgramBinaryCache* cache) : cache_(cache) {}
    ~ProgramStager() = default;
    ProgramStager(const ProgramStager&) = delete;
    ProgramStager& operator=(const ProgramStager&) = delete;

    // The stager the calling thread's builds go through; null outside an Attempt.
    static ProgramStager* current();

    class Attempt {
    public:
        explicit Attempt(ProgramStager& stager);
        ~Attempt();
        Attempt(const Attempt&) = delete;
        Attempt& operator=(const Attempt&) = delete;
        // A placeholder was handed out: the scene built in this attempt is not whole.
        bool deferred() const { return stager_.deferred_; }

    private:
        ProgramStager& stager_;
        ProgramStager* previous_;
    };

    // A current context: picks up GL_KHR_parallel_shader_compile. Anything held belonged to the
    // old context and is forgotten without GL calls.
    void onContext(bool parallelCompile);
    // Inside an Attempt. 0 with `error` set when the program failed to build, as program::build
    // reports it; `placeholder` (optional) says the result is a stand-in.
    GLuint obtain(const std::vector<ShaderStage>& stages, std::string* error, bool* placeholder = nullptr);
    // Outside attempts, once per frame while the stager is not idle.
    void step(double nowS);
    // Nothing queued and nothing compiling.
    bool idle() const;
    // GL thread with the context that made them: deletes everything held.
    void clear();
    // The context is gone: drops everything held without GL calls.
    void forget() { entries_.clear(); }
    bool parallel() const { return parallel_; }

private:
    struct Job {
        GLuint program = 0;
        std::vector<GLuint> shaders;
    };
    struct Entry {
        std::string id;
        std::vector<ShaderStage> stages;
        std::optional<ProgramKey> cacheKey;
        int queued = 0;
        std::vector<GLuint> ready;
        std::vector<Job> inFlight;
        bool failed = false;
        std::string error;
    };

    Entry& entryFor(const std::vector<ShaderStage>& stages);
    Job kick(const Entry& entry) const;
    void finish(Entry& entry, Job job);
    void startQueued(double nowS);
    int inFlightCount() const;

    ProgramBinaryCache* cache_;
    bool parallel_ = false;
    bool deferred_ = false;
    std::vector<Entry> entries_;
};

}  // namespace geode::viz
