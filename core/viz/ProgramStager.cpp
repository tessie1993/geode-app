#include "viz/ProgramStager.hpp"

#include <GLES3/gl31.h>

#include <chrono>

#include "viz/GlProber.hpp"
#include "viz/Program.hpp"
#include "viz/ScenePrep.hpp"
#include "viz/Sha256.hpp"

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

namespace geode::viz {

namespace {

// Per thread, so a live renderer staging on its GL thread never reroutes an export renderer's
// builds on another thread.
thread_local ProgramStager* tCurrent = nullptr;

// Parallel kicks still occupy driver threads and memory; more than this many at once buys nothing.
constexpr int kMaxInFlight = 4;
constexpr size_t kIdBytes = 16;

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string idOf(const std::vector<ShaderStage>& stages) {
    Sha256 digest;
    for (const auto& stage : stages) {
        digest.updateInt(stage.type);
        digest.updateFramed(stage.source);
    }
    return Sha256::hex(digest.digest(), kIdBytes);
}

}  // namespace

ProgramStager* ProgramStager::current() { return tCurrent; }

ProgramStager::Attempt::Attempt(ProgramStager& stager) : stager_(stager), previous_(tCurrent) {
    stager_.deferred_ = false;
    tCurrent = &stager_;
}

ProgramStager::Attempt::~Attempt() { tCurrent = previous_; }

void ProgramStager::onContext(bool parallelCompile) {
    forget();
    parallel_ = parallelCompile;
}

ProgramStager::Entry& ProgramStager::entryFor(const std::vector<ShaderStage>& stages) {
    const std::string id = idOf(stages);
    for (auto& entry : entries_) {
        if (entry.id == id) return entry;
    }
    Entry entry;
    entry.id = id;
    entry.stages = stages;
    // The same key program::build uses, so staged and inline builds share binaries.
    if (cache_ && stages.size() == 2 && stages[0].type == GL_VERTEX_SHADER && stages[1].type == GL_FRAGMENT_SHADER) {
        entry.cacheKey = cache_->keyFor(stages[0].source, stages[1].source);
    }
    entries_.push_back(std::move(entry));
    return entries_.back();
}

GLuint ProgramStager::obtain(const std::vector<ShaderStage>& stages, std::string* error, bool* placeholder) {
    if (placeholder) *placeholder = false;
    Entry& entry = entryFor(stages);
    if (entry.failed) {
        if (error) *error = entry.error;
        return 0;
    }
    if (!entry.ready.empty()) {
        const GLuint program = entry.ready.back();
        entry.ready.pop_back();
        return program;
    }
    // One more copy wanted (a scene may build the same pair twice); the attempt cannot finish.
    entry.queued++;
    deferred_ = true;
    if (placeholder) *placeholder = true;
    return glCreateProgram();
}

int ProgramStager::inFlightCount() const {
    int count = 0;
    for (const auto& entry : entries_) count += static_cast<int>(entry.inFlight.size());
    return count;
}

bool ProgramStager::idle() const {
    for (const auto& entry : entries_) {
        if (entry.queued > 0 || !entry.inFlight.empty()) return false;
    }
    return true;
}

ProgramStager::Job ProgramStager::kick(const Entry& entry) const {
    Job job;
    job.program = glCreateProgram();
    for (const auto& stage : entry.stages) {
        const GLuint shader = glCreateShader(stage.type);
        const char* text = stage.source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        glAttachShader(job.program, shader);
        job.shaders.push_back(shader);
    }
    if (entry.cacheKey) cache_->markRetrievable(job.program);
    // With the parallel extension none of these calls waits; the status queries in finish() would.
    glLinkProgram(job.program);
    return job;
}

void ProgramStager::finish(Entry& entry, Job job) {
    std::string error;
    for (size_t i = 0; i < job.shaders.size() && error.empty(); ++i) {
        GLint status = 0;
        glGetShaderiv(job.shaders[i], GL_COMPILE_STATUS, &status);
        if (status == 0) {
            error = program::infoLog(job.shaders[i], false);
            if (entry.stages[i].type == GL_COMPUTE_SHADER) error = "compute compile failed: " + error;
        }
    }
    if (error.empty()) {
        GLint status = 0;
        glGetProgramiv(job.program, GL_LINK_STATUS, &status);
        if (status == 0) error = "Link failed: " + program::infoLog(job.program, true);
    }
    for (const GLuint shader : job.shaders) {
        glDetachShader(job.program, shader);
        glDeleteShader(shader);
    }
    if (!error.empty()) {
        glDeleteProgram(job.program);
        entry.failed = true;
        entry.error = error;
        entry.queued = 0;
        return;
    }
    if (entry.cacheKey) cache_->store(*entry.cacheKey, job.program);
    entry.ready.push_back(job.program);
}

void ProgramStager::step(double nowS) {
    // Collect what the driver finished in the background first: cheap, and it frees kick slots.
    if (parallel_) {
        for (auto& entry : entries_) {
            for (size_t i = 0; i < entry.inFlight.size();) {
                GLint done = GL_FALSE;
                glGetProgramiv(entry.inFlight[i].program, GL_COMPLETION_STATUS_KHR, &done);
                if (done == GL_FALSE) {
                    ++i;
                    continue;
                }
                Job job = entry.inFlight[i];
                entry.inFlight.erase(entry.inFlight.begin() + static_cast<std::ptrdiff_t>(i));
                if (!entry.failed) {
                    finish(entry, job);
                } else {
                    for (const GLuint shader : job.shaders) glDeleteShader(shader);
                    glDeleteProgram(job.program);
                }
            }
        }
    }
    startQueued(nowS);
    prober::drainErrors();
}

void ProgramStager::startQueued(double nowS) {
    BuildBudget budget;
    budget.begin(nowS);
    for (auto& entry : entries_) {
        while (entry.queued > 0 && !entry.failed) {
            if (entry.cacheKey && budget.allows(nowSeconds(), false)) {
                const GLuint cached = cache_->load(*entry.cacheKey);
                budget.spend(false);
                if (cached != 0) {
                    entry.ready.push_back(cached);
                    entry.queued--;
                    continue;
                }
            }
            const bool blocking = !parallel_;
            if (parallel_ && inFlightCount() >= kMaxInFlight) return;
            if (!budget.allows(nowSeconds(), blocking)) return;
            Job job = kick(entry);
            budget.spend(blocking);
            entry.queued--;
            if (blocking) {
                finish(entry, job);
            } else {
                entry.inFlight.push_back(job);
            }
        }
    }
}

void ProgramStager::clear() {
    for (auto& entry : entries_) {
        for (const GLuint program : entry.ready) glDeleteProgram(program);
        for (const auto& job : entry.inFlight) {
            for (const GLuint shader : job.shaders) glDeleteShader(shader);
            glDeleteProgram(job.program);
        }
    }
    entries_.clear();
}

}  // namespace geode::viz
