#include "viz/ProgramBinaryCache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "util/Log.hpp"
#include "viz/GlProber.hpp"
#include "viz/Sha256.hpp"

namespace geode::viz {

namespace fs = std::filesystem;

namespace {

constexpr const char* kTag = "ProgramCache";
constexpr uint32_t kSchema = 1;
constexpr uint32_t kMagic = 0x47454F44;
constexpr const char* kRootDir = "gl-program-binaries";
constexpr const char* kProbationDir = "new";
constexpr const char* kProvenDir = "kept";
constexpr const char* kSentinelFile = "loading";
constexpr const char* kDisabledFile = "disabled";
constexpr const char* kTempPrefix = "tmp-";
constexpr size_t kHeaderBytes = 16;
constexpr size_t kKeyBytes = 16;
constexpr size_t kNamespaceBytes = 8;
constexpr uintmax_t kMaxTotalBytes = 24ull * 1024ull * 1024ull;
constexpr size_t kMaxEntries = 256;
constexpr GLint kMaxEntryBytes = 4 * 1024 * 1024;
constexpr size_t kWriteQueueLimit = 32;
constexpr int kSweepAfterWrites = 32;

uint32_t readBigEndian(const unsigned char* at) {
    return (static_cast<uint32_t>(at[0]) << 24) | (static_cast<uint32_t>(at[1]) << 16) | (static_cast<uint32_t>(at[2]) << 8) | at[3];
}

void writeBigEndian(std::ofstream& out, uint32_t value) {
    const unsigned char bytes[4] = {static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
                                    static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool ensureDir(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return fs::is_directory(dir, ec);
}

std::vector<fs::directory_entry> filesIn(const std::string& dir) {
    std::vector<fs::directory_entry> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec)) out.push_back(entry);
    }
    return out;
}

}  // namespace

ProgramBinaryCache::ProgramBinaryCache() : writer_([this] { writerLoop(); }) {}

ProgramBinaryCache::~ProgramBinaryCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (writer_.joinable()) writer_.join();
}

void ProgramBinaryCache::install(const std::string& appCacheDir) {
    if (!root_.empty()) return;
    root_ = appCacheDir + "/" + kRootDir;
}

void ProgramBinaryCache::prime() {
    if (state_ != State::Unprimed || root_.empty()) return;
    openStore();
    if (state_ == State::On) submit([this] { prune(); });
}

std::optional<ProgramKey> ProgramBinaryCache::keyFor(const std::string& vertexSrc, const std::string& fragmentSrc) {
    prime();
    if (state_ != State::On) return std::nullopt;
    Sha256 digest;
    digest.updateInt(kSchema);
    digest.updateFramed(vertexSrc);
    digest.updateFramed(fragmentSrc);
    return ProgramKey{Sha256::hex(digest.digest(), kKeyBytes)};
}

void ProgramBinaryCache::markRetrievable(GLuint program) {
    if (state_ != State::On) return;
    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
}

GLuint ProgramBinaryCache::load(const ProgramKey& key) {
    if (state_ != State::On) return 0;
    const std::string promoted = proven_ + "/" + key.hex;
    const bool fromProbation = !isFile(promoted);
    const std::string file = fromProbation ? probation_ + "/" + key.hex : promoted;
    if (!isFile(file)) return 0;

    const auto entry = readEntry(file);
    if (!entry) {
        submit([file] { std::remove(file.c_str()); });
        return 0;
    }
    const GLuint program = glCreateProgram();
    if (program == 0) return 0;
    loadGuarded(program, *entry);
    prober::drainErrors();

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == 0) {
        glDeleteProgram(program);
        GEODE_LOGW(kTag, "driver rejected its own program binary; recompiling from source");
        submit([file] { std::remove(file.c_str()); });
        return 0;
    }
    touch(file, fromProbation);
    return program;
}

void ProgramBinaryCache::store(const ProgramKey& key, GLuint program) {
    if (state_ != State::On) return;
    GLint size = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &size);
    if (size <= 0 || size > kMaxEntryBytes) return;
    std::vector<unsigned char> payload(static_cast<size_t>(size));
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(program, size, &written, &format, payload.data());
    prober::drainErrors();
    if (written <= 0 || written > size) return;
    payload.resize(static_cast<size_t>(written));
    submit([this, key, format, payload = std::move(payload)]() mutable { write(key, format, std::move(payload)); });
}

void ProgramBinaryCache::openStore() {
    const GlIdentity identity = prober::identity();
    GLint count = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
    prober::drainErrors();
    if (count <= 0) {
        state_ = State::Off;
        GEODE_LOGI(kTag, "program binary cache off: driver exposes no program binary formats");
        return;
    }
    formats_.assign(static_cast<size_t>(count), 0);
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats_.data());
    prober::drainErrors();

    Sha256 digest;
    digest.updateInt(kSchema);
    digest.updateFramed(identity.vendor);
    digest.updateFramed(identity.renderer);
    digest.updateFramed(identity.versionString);
    namespace_ = root_ + "/ns-" + Sha256::hex(digest.digest(), kNamespaceBytes);

    if (isFile(namespace_ + "/" + kDisabledFile)) {
        state_ = State::Off;
        GEODE_LOGI(kTag, "program binary cache off: a previous run did not survive glProgramBinary on this driver");
        return;
    }
    sentinel_ = namespace_ + "/" + kSentinelFile;
    if (isFile(sentinel_)) {
        GEODE_LOGW(kTag, "a previous run died inside glProgramBinary; disabling the cache for this driver");
        std::error_code ec;
        fs::remove_all(namespace_, ec);
        ensureDir(namespace_);
        std::ofstream(namespace_ + "/" + kDisabledFile).close();
        state_ = State::Off;
        return;
    }
    probation_ = namespace_ + "/" + kProbationDir;
    proven_ = namespace_ + "/" + kProvenDir;
    if (!ensureDir(probation_) || !ensureDir(proven_)) {
        state_ = State::Off;
        GEODE_LOGI(kTag, "program binary cache off: could not create %s", namespace_.c_str());
        return;
    }
    state_ = State::On;
    GEODE_LOGI(kTag, "program binary cache at %s, %d format(s)", fs::path(namespace_).filename().c_str(), count);
}

std::optional<ProgramBinaryCache::Entry> ProgramBinaryCache::readEntry(const std::string& file) const {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() <= kHeaderBytes) return std::nullopt;
    const uint32_t magic = readBigEndian(bytes.data());
    const uint32_t schema = readBigEndian(bytes.data() + 4);
    const uint32_t format = readBigEndian(bytes.data() + 8);
    const uint32_t length = readBigEndian(bytes.data() + 12);
    if (magic != kMagic || schema != kSchema) return std::nullopt;
    if (length == 0 || bytes.size() - kHeaderBytes != length) return std::nullopt;
    if (std::find(formats_.begin(), formats_.end(), static_cast<GLint>(format)) == formats_.end()) return std::nullopt;
    Entry entry;
    entry.format = format;
    entry.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes), bytes.end());
    return entry;
}

void ProgramBinaryCache::loadGuarded(GLuint program, const Entry& entry) {
    const bool arming = !sentinelSpent_;
    if (arming) {
        sentinelSpent_ = true;
        std::ofstream(sentinel_).close();
    }
    glProgramBinary(program, entry.format, entry.payload.data(), static_cast<GLsizei>(entry.payload.size()));
    if (arming) std::remove(sentinel_.c_str());
}

void ProgramBinaryCache::submit(std::function<void()> work) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= kWriteQueueLimit) return;
        queue_.push_back(std::move(work));
    }
    wake_.notify_one();
}

void ProgramBinaryCache::writerLoop() {
    while (true) {
        std::function<void()> work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
    }
}

void ProgramBinaryCache::touch(const std::string& file, bool fromProbation) {
    const std::string proven = proven_;
    submit([file, fromProbation, proven] {
        std::error_code ec;
        std::string landed = file;
        if (fromProbation) {
            landed = proven + "/" + fs::path(file).filename().string();
            fs::rename(file, landed, ec);
        }
        fs::last_write_time(landed, fs::file_time_type::clock::now(), ec);
    });
}

void ProgramBinaryCache::write(const ProgramKey& key, GLenum format, std::vector<unsigned char> payload) {
    if (isFile(proven_ + "/" + key.hex)) return;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string temp = namespace_ + "/" + kTempPrefix + key.hex + "-" + std::to_string(stamp);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        writeBigEndian(out, kMagic);
        writeBigEndian(out, kSchema);
        writeBigEndian(out, format);
        writeBigEndian(out, static_cast<uint32_t>(payload.size()));
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    std::error_code ec;
    fs::rename(temp, probation_ + "/" + key.hex, ec);
    if (ec) {
        std::remove(temp.c_str());
        return;
    }
    writesSinceSweep_++;
    bool idle;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        idle = queue_.empty();
    }
    if (idle || writesSinceSweep_ >= kSweepAfterWrites) {
        writesSinceSweep_ = 0;
        sweep();
    }
}

void ProgramBinaryCache::sweep() {
    auto probation = filesIn(probation_);
    auto proven = filesIn(proven_);
    std::error_code ec;
    uintmax_t bytes = 0;
    for (const auto& f : probation) bytes += f.file_size(ec);
    for (const auto& f : proven) bytes += f.file_size(ec);
    size_t count = probation.size() + proven.size();
    if (bytes <= kMaxTotalBytes && count <= kMaxEntries) return;
    auto byAge = [&](const fs::directory_entry& a, const fs::directory_entry& b) { return a.last_write_time(ec) < b.last_write_time(ec); };
    std::sort(probation.begin(), probation.end(), byAge);
    std::sort(proven.begin(), proven.end(), byAge);
    std::vector<fs::directory_entry> order = probation;
    order.insert(order.end(), proven.begin(), proven.end());
    for (const auto& file : order) {
        if (bytes <= kMaxTotalBytes && count <= kMaxEntries) return;
        const uintmax_t size = file.file_size(ec);
        if (fs::remove(file.path(), ec)) {
            bytes -= size;
            count--;
        }
    }
}

void ProgramBinaryCache::prune() {
    std::error_code ec;
    const std::string live = fs::path(namespace_).filename().string();
    for (const auto& entry : fs::directory_iterator(root_, ec)) {
        if (entry.path().filename().string() == live) continue;
        fs::remove_all(entry.path(), ec);
    }
    for (const auto& entry : fs::directory_iterator(namespace_, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(kTempPrefix, 0) != 0) continue;
        fs::remove(entry.path(), ec);
    }
}

}  // namespace geode::viz
