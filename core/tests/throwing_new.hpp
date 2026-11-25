// A global operator new that throws std::bad_alloc while armed, for checking that a C ABI entry answers its
// failure value when memory runs out rather than letting the exception reach JNI, where it ends the process.
// Include it in exactly one file of a test binary: it replaces the program's allocator.
#pragma once
#include <cstdlib>
#include <new>

namespace oom {
inline bool armed = false;
inline int thrown = 0;

// Arms the allocator for one scope and counts what it refused.
struct Armed {
    Armed() {
        thrown = 0;
        armed = true;
    }
    ~Armed() { armed = false; }
    Armed(const Armed&) = delete;
    Armed& operator=(const Armed&) = delete;
};
}  // namespace oom

void* operator new(std::size_t n) {
    if (oom::armed) {
        ++oom::thrown;
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }

void operator delete(void* p, std::size_t) noexcept { std::free(p); }
