#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace geode::audio::player {

// Single-producer single-consumer ring: wait-free, allocation only in the constructor.
template <typename T>
class SpscRing {
public:
    explicit SpscRing(size_t capacity) : buffer_(roundUp(capacity)), mask_(buffer_.size() - 1) {}

    size_t capacity() const { return buffer_.size(); }

    size_t readable() const {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }

    size_t writable() const { return capacity() - readable(); }

    // Producer only. Returns how many items fitted.
    size_t push(const T* items, size_t count) {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        const size_t n = std::min(count, capacity() - (w - r));
        copyIn(items, w, n);
        write_.store(w + n, std::memory_order_release);
        return n;
    }

    // Consumer only. Returns how many items were read.
    size_t pop(T* out, size_t count) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t n = std::min(count, w - r);
        copyOut(out, r, n);
        read_.store(r + n, std::memory_order_release);
        return n;
    }

private:
    static size_t roundUp(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void copyIn(const T* items, size_t start, size_t n) {
        const size_t first = std::min(n, capacity() - (start & mask_));
        std::copy_n(items, first, buffer_.data() + (start & mask_));
        std::copy_n(items + first, n - first, buffer_.data());
    }

    void copyOut(T* out, size_t start, size_t n) {
        const size_t first = std::min(n, capacity() - (start & mask_));
        std::copy_n(buffer_.data() + (start & mask_), first, out);
        std::copy_n(buffer_.data(), n - first, out + first);
    }

    std::vector<T> buffer_;
    size_t mask_;
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

}  // namespace geode::audio::player
