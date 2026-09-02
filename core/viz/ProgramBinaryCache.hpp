#pragma once
#include <GLES3/gl3.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace geode::viz {

struct ProgramKey {
    std::string hex;
};

// Persistent glGetProgramBinary/glProgramBinary cache; same directory and entry layout as ProgramBinaryCache.kt.
class ProgramBinaryCache {
public:
    ProgramBinaryCache();
    ~ProgramBinaryCache();
    ProgramBinaryCache(const ProgramBinaryCache&) = delete;
    ProgramBinaryCache& operator=(const ProgramBinaryCache&) = delete;

    void install(const std::string& appCacheDir);
    void prime();
    std::optional<ProgramKey> keyFor(const std::string& vertexSrc, const std::string& fragmentSrc);
    void markRetrievable(GLuint program);
    GLuint load(const ProgramKey& key);
    void store(const ProgramKey& key, GLuint program);

private:
    enum class State { Unprimed, Off, On };
    struct Entry {
        GLenum format;
        std::vector<unsigned char> payload;
    };

    void openStore();
    std::optional<Entry> readEntry(const std::string& file) const;
    void loadGuarded(GLuint program, const Entry& entry);
    void submit(std::function<void()> work);
    void writerLoop();
    void write(const ProgramKey& key, GLenum format, std::vector<unsigned char> payload);
    void sweep();
    void prune();
    void touch(const std::string& file, bool fromProbation);

    std::string root_;
    State state_ = State::Unprimed;
    std::string namespace_;
    std::string probation_;
    std::string proven_;
    std::string sentinel_;
    std::vector<GLint> formats_;
    bool sentinelSpent_ = false;
    int writesSinceSweep_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::thread writer_;
};

}  // namespace geode::viz
