#include "scene/object/ModelTextures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

// Basis Universal, unmodified (third_party/basis_universal): the PNG and JPEG readers it ships
// (pvpngreader, jpgd, both public domain), its UASTC encoder, and its transcoder for KTX2 and for
// UASTC -> ASTC / ETC2.
#include "encoder/basisu_enc.h"
#include "encoder/basisu_uastc_enc.h"
#include "encoder/jpgd.h"
#include "encoder/pvpngreader.h"
#include "transcoder/basisu_transcoder.h"

namespace geode::scene::object {
namespace {

constexpr uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint8_t kKtx2Magic[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

// The smallest PNG pvpngreader takes (signature, IHDR, one IDAT byte, IEND). Checked here because the
// reader asserts on less instead of failing.
constexpr std::size_t kMinPngBytes = 8 + 13 + 8 + 1 + 4 + 12;

// "Faster" is UASTC's second level: about 46 dB against the source, at a fraction of the default
// level's time. An import encodes up to forty textures on a phone; the step to the default buys
// little that a model seen across a Scene shows.
constexpr uint32_t kUastcFlags = basisu::cPackUASTCLevelFaster;

constexpr std::size_t kBlockBytes = 16;

uint32_t be16(const uint8_t* p) { return (uint32_t{p[0]} << 8) | p[1]; }
uint32_t be32(const uint8_t* p) { return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]; }

bool isColour(TextureRole role) { return role == TextureRole::BaseColour || role == TextureRole::Emissive; }

uint32_t blocks(uint32_t texels) { return (texels + 3) / 4; }

std::size_t levelBytes(uint32_t w, uint32_t h) { return std::size_t{blocks(w)} * blocks(h) * kBlockBytes; }

uint32_t levelSize(uint32_t top, uint32_t level) { return std::max<uint32_t>(1, top >> std::min<uint32_t>(level, 31)); }

uint32_t mipCount(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    for (uint32_t s = std::max(w, h); s > 1; s >>= 1) ++n;
    return n;
}

// The image size from the header alone, so a texture can be planned (and an oversized one dropped)
// before a byte of it is decoded.
bool pngSize(const uint8_t* b, std::size_t size, uint32_t& w, uint32_t& h) {
    if (size < kMinPngBytes || std::memcmp(b + 12, "IHDR", 4) != 0) return false;
    w = be32(b + 16);
    h = be32(b + 20);
    return w > 0 && h > 0;
}

// Walks the JPEG markers to the first start-of-frame. DHT (C4), JPG (C8) and DAC (CC) share the
// SOFn range but are tables, not frames.
bool jpegSize(const uint8_t* b, std::size_t size, uint32_t& w, uint32_t& h) {
    std::size_t pos = 2;
    while (pos + 4 <= size) {
        if (b[pos] != 0xFF) return false;
        const uint8_t marker = b[pos + 1];
        if (marker == 0xFF) {
            ++pos;  // fill byte
            continue;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {
            pos += 2;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) return false;  // scan or end before any frame
        const uint32_t length = be16(b + pos + 2);
        if (length < 2) return false;
        const bool frame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (frame) {
            if (pos + 9 > size) return false;
            h = be16(b + pos + 5);
            w = be16(b + pos + 7);
            return w > 0 && h > 0;
        }
        pos += 2 + length;
    }
    return false;
}

// A KTX2 the transcoder can read as one plain 2D LDR image: glTF textures are never arrays or cubes,
// and an HDR one has no place in a model's material.
bool ktx2Open(basist::ktx2_transcoder& t, const uint8_t* b, std::size_t size, std::string& why) {
    if (size > UINT32_MAX || !t.init(b, static_cast<uint32_t>(size))) {
        why = "unreadable KTX2";
        return false;
    }
    if (t.get_faces() != 1 || t.get_layers() > 1) {
        why = "KTX2 is a cube map or an array";
        return false;
    }
    if (!t.is_ldr()) {
        why = "KTX2 is HDR";
        return false;
    }
    return true;
}

struct Image {
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector<uint8_t> px;  // RGBA8, rows top to bottom
};

const std::array<float, 256>& srgbToLinear() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); }

uint8_t linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return toByte(c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

// Area-average weights from n source texels onto m output texels (m <= n): each output texel takes
// the share of every source texel its footprint covers, so any ratio, odd mip sizes included, keeps
// the image's energy.
struct Taps {
    std::vector<uint32_t> start;  // per output texel, into index/weight (one extra at the end)
    std::vector<uint32_t> index;
    std::vector<float> weight;
};

Taps taps(uint32_t n, uint32_t m) {
    Taps t;
    const double scale = static_cast<double>(n) / static_cast<double>(m);
    t.start.reserve(m + 1);
    for (uint32_t o = 0; o < m; ++o) {
        t.start.push_back(static_cast<uint32_t>(t.index.size()));
        const double begin = o * scale, end = (o + 1) * scale;
        for (auto i = static_cast<uint32_t>(begin); i < n && i < end; ++i) {
            const double cover = std::min(end, i + 1.0) - std::max(begin, static_cast<double>(i));
            if (cover <= 0.0) continue;
            t.index.push_back(i);
            t.weight.push_back(static_cast<float>(cover / scale));
        }
    }
    t.start.push_back(static_cast<uint32_t>(t.index.size()));
    return t;
}

// Box-filters `src` to tw x th. Colour roles average in linear light (sRGB averaged as stored darkens
// every edge); normal maps are renormalised, since an average of unit vectors is shorter than one.
// Row by row, so the working memory is two output rows of floats, not a float copy of the image.
Image resample(const Image& src, uint32_t tw, uint32_t th, TextureRole role) {
    if (tw == src.w && th == src.h) return src;
    const bool srgb = isColour(role);
    const auto& lin = srgbToLinear();
    const Taps tx = taps(src.w, tw), ty = taps(src.h, th);
    Image out{tw, th, std::vector<uint8_t>(std::size_t{tw} * th * 4)};
    std::vector<float> row(std::size_t{tw} * 4), acc(std::size_t{tw} * 4);
    for (uint32_t oy = 0; oy < th; ++oy) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        for (uint32_t k = ty.start[oy]; k < ty.start[oy + 1]; ++k) {
            const uint8_t* line = &src.px[std::size_t{ty.index[k]} * src.w * 4];
            std::fill(row.begin(), row.end(), 0.0f);
            for (uint32_t ox = 0; ox < tw; ++ox) {
                float* r = &row[std::size_t{ox} * 4];
                for (uint32_t j = tx.start[ox]; j < tx.start[ox + 1]; ++j) {
                    const uint8_t* p = line + std::size_t{tx.index[j]} * 4;
                    const float w = tx.weight[j];
                    for (int c = 0; c < 3; ++c) r[c] += w * (srgb ? lin[p[c]] : p[c] / 255.0f);
                    r[3] += w * (p[3] / 255.0f);
                }
            }
            const float wy = ty.weight[k];
            for (std::size_t i = 0; i < acc.size(); ++i) acc[i] += wy * row[i];
        }
        uint8_t* dst = &out.px[std::size_t{oy} * tw * 4];
        for (uint32_t ox = 0; ox < tw; ++ox) {
            float* a = &acc[std::size_t{ox} * 4];
            if (role == TextureRole::Normal) {
                float n[3] = {a[0] * 2.0f - 1.0f, a[1] * 2.0f - 1.0f, a[2] * 2.0f - 1.0f};
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-6f) {
                    for (int c = 0; c < 3; ++c) a[c] = n[c] / len * 0.5f + 0.5f;
                }
            }
            for (int c = 0; c < 3; ++c) dst[ox * 4 + c] = srgb ? linearToSrgb(a[c]) : toByte(a[c]);
            dst[ox * 4 + 3] = toByte(a[3]);
        }
    }
    return out;
}

bool fromMalloc(void* pixels, uint32_t w, uint32_t h, Image& out) {
    if (pixels == nullptr) return false;
    const auto* p = static_cast<const uint8_t*>(pixels);
    out.w = w;
    out.h = h;
    out.px.assign(p, p + std::size_t{w} * h * 4);
    std::free(pixels);
    return w > 0 && h > 0;
}

// Two statements on purpose: load_png fills w and h through references and fromMalloc takes them by
// value, so in one call the compiler may copy them (still 0) before load_png runs. GCC does.
bool decodePng(const uint8_t* b, std::size_t size, Image& out) {
    uint32_t w = 0, h = 0, chans = 0;
    void* px = pv_png::load_png(b, size, 4, w, h, chans);
    return fromMalloc(px, w, h, out);
}

bool decodeJpeg(const uint8_t* b, std::size_t size, Image& out) {
    if (size > INT32_MAX) return false;
    int w = 0, h = 0, comps = 0;
    unsigned char* px = jpgd::decompress_jpeg_image_from_memory(b, static_cast<int>(size), &w, &h, &comps, 4);
    return fromMalloc(px, static_cast<uint32_t>(std::max(w, 0)), static_cast<uint32_t>(std::max(h, 0)), out);
}

// Reads the KTX2 level closest above the planned top, so a file that already carries mips is never
// decoded at a size it will only be shrunk from.
bool decodeKtx2(const uint8_t* b, std::size_t size, uint32_t topW, uint32_t topH, uint64_t maxPixels, Image& out,
                std::string& why) {
    basist::ktx2_transcoder t;
    if (!ktx2Open(t, b, size, why)) return false;
    uint32_t level = 0;
    for (uint32_t l = 1; l < t.get_levels(); ++l) {
        basist::ktx2_image_level_info info{};
        if (!t.get_image_level_info(info, l, 0, 0)) break;
        if (info.m_orig_width < topW || info.m_orig_height < topH) break;
        level = l;
    }
    basist::ktx2_image_level_info info{};
    if (!t.get_image_level_info(info, level, 0, 0) || info.m_orig_width == 0 || info.m_orig_height == 0) {
        why = "unreadable KTX2";
        return false;
    }
    const uint64_t pixels = uint64_t{info.m_orig_width} * info.m_orig_height;
    if (pixels > maxPixels) {
        why = std::to_string(info.m_orig_width) + "x" + std::to_string(info.m_orig_height) + " is over the decode limit";
        return false;
    }
    out.w = info.m_orig_width;
    out.h = info.m_orig_height;
    out.px.assign(pixels * 4, 0);
    if (!t.start_transcoding() ||
        !t.transcode_image_level(level, 0, 0, out.px.data(), static_cast<uint32_t>(pixels), basist::transcoder_texture_format::cTFRGBA32)) {
        why = "KTX2 did not transcode";
        return false;
    }
    return true;
}

// One level through UASTC into ASTC 4x4 and ETC2. Edge blocks repeat the last row and column, the
// texels a sampler can reach of a partial block.
bool encodeLevel(const Image& img, bool alpha, TextureLevel& out) {
    const uint32_t bx = blocks(img.w), by = blocks(img.h);
    const std::size_t etcBlock = alpha ? 16 : 8;
    out.width = img.w;
    out.height = img.h;
    out.astc.assign(std::size_t{bx} * by * kBlockBytes, 0);
    out.etc2.assign(std::size_t{bx} * by * etcBlock, 0);
    uint8_t texels[64];
    for (uint32_t y = 0; y < by; ++y) {
        for (uint32_t x = 0; x < bx; ++x) {
            for (uint32_t py = 0; py < 4; ++py) {
                const uint32_t sy = std::min(y * 4 + py, img.h - 1);
                for (uint32_t px = 0; px < 4; ++px) {
                    const uint32_t sx = std::min(x * 4 + px, img.w - 1);
                    std::memcpy(&texels[(py * 4 + px) * 4], &img.px[(std::size_t{sy} * img.w + sx) * 4], 4);
                }
            }
            basist::uastc_block block;
            basisu::encode_uastc(texels, block, kUastcFlags);
            const std::size_t i = std::size_t{y} * bx + x;
            if (!basist::transcode_uastc_to_astc(block, &out.astc[i * kBlockBytes])) return false;
            const bool etc = alpha ? basist::transcode_uastc_to_etc2_rgba(block, &out.etc2[i * etcBlock])
                                   : basist::transcode_uastc_to_etc1(block, &out.etc2[i * etcBlock]);
            if (!etc) return false;
        }
    }
    return true;
}

// The size one texture will be made at, before anything is decoded.
struct Plan {
    std::size_t source = 0;
    ImageCodec codec = ImageCodec::Unknown;
    uint32_t srcW = 0, srcH = 0;
    uint32_t topW = 0, topH = 0;  // the flagship's level 0
    uint32_t levels = 1;
    uint32_t lowBase = 0;

    std::size_t bytesFrom(uint32_t base) const {
        std::size_t total = 0;
        for (uint32_t l = base; l < levels; ++l) total += levelBytes(levelSize(topW, l), levelSize(topH, l));
        return total;
    }
    // Gives up the top level: the next one down becomes the flagship's level 0.
    void dropTop() {
        topW = levelSize(topW, 1);
        topH = levelSize(topH, 1);
        levels = mipCount(topW, topH);
    }
};

// Longest side to `cap`, aspect kept; never upscaled.
void fitSide(uint32_t w, uint32_t h, uint32_t cap, uint32_t& tw, uint32_t& th) {
    const uint32_t longest = std::max(w, h);
    if (longest <= cap) {
        tw = w;
        th = h;
        return;
    }
    const double s = static_cast<double>(cap) / longest;
    tw = w >= h ? cap : std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(w * s)));
    th = h >= w ? cap : std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(h * s)));
}

// The first level whose longest side fits `cap`.
uint32_t baseForCap(const Plan& p, uint32_t cap) {
    uint32_t base = 0;
    while (base + 1 < p.levels && std::max(levelSize(p.topW, base), levelSize(p.topH, base)) > cap) ++base;
    return base;
}

std::string dims(uint32_t w, uint32_t h) { return std::to_string(w) + "x" + std::to_string(h); }

}  // namespace

bool ModelTexture::downscaled() const {
    return !levels.empty() && (levels[0].width < sourceWidth || levels[0].height < sourceHeight);
}

std::size_t ModelTexture::gpuBytes(bool lowDetail) const {
    std::size_t total = 0;
    for (std::size_t l = lowDetail ? lowDetailBaseLevel : 0; l < levels.size(); ++l) total += levels[l].astc.size();
    return total;
}

ImageCodec sniffImage(const uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr) return ImageCodec::Unknown;
    if (size >= sizeof(kPngMagic) && std::memcmp(bytes, kPngMagic, sizeof(kPngMagic)) == 0) return ImageCodec::Png;
    if (size >= sizeof(kKtx2Magic) && std::memcmp(bytes, kKtx2Magic, sizeof(kKtx2Magic)) == 0) return ImageCodec::Ktx2;
    if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return ImageCodec::Jpeg;
    return ImageCodec::Unknown;
}

TextureBuild buildTextures(const std::vector<TextureSource>& sources, const TextureLimits& limits) {
    TextureBuild out;
    out.textureOfSource.assign(sources.size(), -1);
    if (sources.empty()) return out;
    // Once per process (the library guards it with its own mutex): the transcoder's and the UASTC
    // encoder's tables. Never the OpenCL path or the job pool.
    basisu::basisu_encoder_init(false, false);

    auto drop = [&out](std::size_t source, std::string why) { out.drops.push_back({source, std::move(why)}); };

    // 1. Plan every texture from its header: the role's cap, then a full mip chain under it.
    std::vector<Plan> plans;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const TextureSource& s = sources[i];
        Plan p;
        p.source = i;
        p.codec = sniffImage(s.bytes, s.size);
        bool sized = false;
        std::string why;
        switch (p.codec) {
            case ImageCodec::Png: sized = pngSize(s.bytes, s.size, p.srcW, p.srcH); break;
            case ImageCodec::Jpeg: sized = jpegSize(s.bytes, s.size, p.srcW, p.srcH); break;
            case ImageCodec::Ktx2: {
                basist::ktx2_transcoder t;
                sized = ktx2Open(t, s.bytes, s.size, why);
                if (sized) {
                    p.srcW = t.get_width();
                    p.srcH = t.get_height();
                }
                break;
            }
            case ImageCodec::Unknown: drop(i, "not a PNG, JPEG or KTX2 image"); continue;
        }
        if (!sized) {
            drop(i, why.empty() ? "unreadable image header" : why);
            continue;
        }
        if (p.codec != ImageCodec::Ktx2 && uint64_t{p.srcW} * p.srcH > limits.maxSourcePixels) {
            drop(i, dims(p.srcW, p.srcH) + " is over the decode limit");
            continue;
        }
        const uint32_t cap = s.role == TextureRole::BaseColour ? limits.flagship.baseColourSize : limits.flagship.otherSize;
        fitSide(p.srcW, p.srcH, std::max<uint32_t>(cap, 1), p.topW, p.topH);
        p.levels = mipCount(p.topW, p.topH);
        plans.push_back(p);
    }

    // 2. The memory budget, fitted: the flagship gives up top levels, then the simplified mode starts
    //    lower, each time on the texture whose top level is the largest.
    auto total = [&plans](bool low) {
        std::size_t sum = 0;
        for (const Plan& p : plans) sum += p.bytesFrom(low ? p.lowBase : 0);
        return sum;
    };
    out.bytesBeforeFit = total(false);
    while (total(false) > limits.flagship.budgetBytes) {
        Plan* largest = nullptr;
        std::size_t largestBytes = 0;
        for (Plan& p : plans) {
            const std::size_t top = levelBytes(p.topW, p.topH);
            if (p.levels > 1 && top > largestBytes) {
                largest = &p;
                largestBytes = top;
            }
        }
        if (largest == nullptr) break;
        largest->dropTop();
        out.fittedToBudget = true;
    }
    for (Plan& p : plans) {
        const uint32_t cap = sources[p.source].role == TextureRole::BaseColour ? limits.simplified.baseColourSize
                                                                                : limits.simplified.otherSize;
        p.lowBase = baseForCap(p, std::max<uint32_t>(cap, 1));
    }
    while (total(true) > limits.simplified.budgetBytes) {
        Plan* largest = nullptr;
        std::size_t largestBytes = 0;
        for (Plan& p : plans) {
            const std::size_t top = levelBytes(levelSize(p.topW, p.lowBase), levelSize(p.topH, p.lowBase));
            if (p.lowBase + 1 < p.levels && top > largestBytes) {
                largest = &p;
                largestBytes = top;
            }
        }
        if (largest == nullptr) break;
        ++largest->lowBase;
        out.fittedToBudget = true;
    }
    if (total(false) > limits.flagship.budgetBytes || total(true) > limits.simplified.budgetBytes) {
        out.overBudget = true;
        return out;
    }

    // 3. Make each texture, one at a time, so only one decoded image is ever held.
    for (const Plan& p : plans) {
        const TextureSource& s = sources[p.source];
        Image img;
        std::string why;
        bool decoded = false;
        switch (p.codec) {
            case ImageCodec::Png: decoded = decodePng(s.bytes, s.size, img); break;
            case ImageCodec::Jpeg: decoded = decodeJpeg(s.bytes, s.size, img); break;
            case ImageCodec::Ktx2: decoded = decodeKtx2(s.bytes, s.size, p.topW, p.topH, limits.maxSourcePixels, img, why); break;
            case ImageCodec::Unknown: break;
        }
        if (!decoded) {
            drop(p.source, why.empty() ? "image did not decode" : why);
            continue;
        }
        ModelTexture tex;
        tex.role = s.role;
        tex.source = p.codec;
        tex.srgb = isColour(s.role);
        tex.sourceWidth = p.srcW;
        tex.sourceHeight = p.srcH;
        tex.lowDetailBaseLevel = p.lowBase;
        // Only base colour carries alpha into the material (glTF reads the others' RGB alone), so the
        // rest are made opaque: the encoder then spends no bits on it, and ETC2 needs no EAC block.
        for (std::size_t i = 3; i < img.px.size(); i += 4) {
            if (s.role != TextureRole::BaseColour) {
                img.px[i] = 255;
            } else if (img.px[i] != 255) {
                tex.hasAlpha = true;
            }
        }
        Image level = resample(img, p.topW, p.topH, s.role);
        img = Image{};
        bool encoded = true;
        tex.levels.resize(p.levels);
        for (uint32_t l = 0; l < p.levels && encoded; ++l) {
            if (l > 0) level = resample(level, levelSize(p.topW, l), levelSize(p.topH, l), s.role);
            encoded = encodeLevel(level, tex.hasAlpha, tex.levels[l]);
        }
        if (!encoded) {
            drop(p.source, "encode failed");
            continue;
        }
        out.bytesFlagship += tex.gpuBytes(false);
        out.bytesLowDetail += tex.gpuBytes(true);
        out.textureOfSource[p.source] = static_cast<int>(out.textures.size());
        out.textures.push_back(std::move(tex));
    }
    return out;
}

}  // namespace geode::scene::object
