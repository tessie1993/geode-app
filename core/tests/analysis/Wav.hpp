#pragma once
// A RIFF/WAVE reader for the analysis bench and the offline analyser's golden test: PCM16 only, which is
// all tools/analysis_lab writes. Samples are read as s / 32768, the same scaling libsndfile (and so
// librosa.load) uses, so both sides analyse the same floats.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace geode::analysis {

struct Wav {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> interleaved;  // frames × channels

    size_t frames() const { return channels > 0 ? interleaved.size() / static_cast<size_t>(channels) : 0; }

    // (L + R) / 2 for stereo, as librosa.load(mono=True) folds; the channel mean for any count.
    std::vector<float> mono() const {
        std::vector<float> out(frames());
        for (size_t i = 0; i < out.size(); ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += interleaved[i * channels + c];
            out[i] = sum / static_cast<float>(channels);
        }
        return out;
    }
};

inline uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// Empty Wav (sampleRate 0) when the file is missing or not PCM16.
inline Wav readWav(const std::string& path) {
    Wav wav;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return wav;
    std::vector<unsigned char> bytes;
    unsigned char buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + got);
    std::fclose(f);
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return wav;
    int bits = 0, format = 0, channels = 0, rate = 0;
    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* chunk = bytes.data() + pos;
        const size_t size = le32(chunk + 4);
        const size_t body = pos + 8;
        if (body + size > bytes.size()) break;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = static_cast<int>(le32(chunk + 12));
            bits = le16(chunk + 22);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (format != 1 || bits != 16 || channels <= 0) return wav;
            const size_t count = size / 2;
            wav.interleaved.resize(count);
            for (size_t i = 0; i < count; ++i) {
                const auto s = static_cast<int16_t>(le16(bytes.data() + body + 2 * i));
                wav.interleaved[i] = static_cast<float>(s) / 32768.0f;
            }
            wav.interleaved.resize(count - count % static_cast<size_t>(channels));
            wav.sampleRate = rate;
            wav.channels = channels;
            return wav;
        }
        pos = body + size + (size & 1);  // chunks are word-aligned
    }
    return wav;
}

}  // namespace geode::analysis
