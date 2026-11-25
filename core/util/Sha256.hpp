#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace geode::viz {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t length);
    void updateInt(uint32_t value);
    void updateFramed(const std::string& text);
    std::array<uint8_t, 32> digest();
    static std::string hex(const std::array<uint8_t, 32>& digest, size_t bytes);

private:
    void transform(const uint8_t* block);

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_{};
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

}  // namespace geode::viz
