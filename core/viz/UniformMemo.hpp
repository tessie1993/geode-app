#pragma once
#include <cstddef>
#include <cstring>
#include <vector>

namespace geode::viz {

// What the last glUniform* call sent to each uniform of one program, so a per-frame upload of an unchanged value is
// skipped: a uniform keeps its value in the program until it is linked again. GL-free, so a host test can drive it.
//
// One memo belongs to one UniformCache, which belongs to one program: a relinked or rebuilt program is a new
// UniformCache, a lost context drops it (forget), so a stale value is never compared against a new program's.
// Only the uploads that go through the cache are remembered; a uniform that is also written some other way must not
// use it. The bytes are compared as bytes, so a NaN or a -0 is sent like any other change.
class UniformMemo {
public:
    explicit UniformMemo(size_t slots = 0) : slots_(slots) {}

    // True when `bytes` of `data` differ from what this slot last sent (or it sent nothing yet), and remembers them.
    bool changed(size_t slot, const void* data, size_t bytes) {
        Entry& e = slots_[slot];
        if (e.valid && e.bytes.size() == bytes && std::memcmp(e.bytes.data(), data, bytes) == 0) return false;
        const auto* p = static_cast<const unsigned char*>(data);
        e.bytes.assign(p, p + bytes);
        e.valid = true;
        return true;
    }

    // Every value is unknown again: the next upload of each is sent.
    void clear() {
        for (Entry& e : slots_) e.valid = false;
    }

private:
    struct Entry {
        std::vector<unsigned char> bytes;
        bool valid = false;
    };
    std::vector<Entry> slots_;
};

}  // namespace geode::viz
