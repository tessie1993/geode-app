#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The textures of an imported model (#474 part 2, limits from #455).
//
// An imported model's textures arrive as PNG or JPEG in the .glb's buffer views, or as KTX2 through
// KHR_texture_basisu. None of those can go to the GPU as they are: a 4096² PNG is 64 MB once
// decoded, and a phone has no PNG sampler. So every image is made once, here, into what the Scene's
// overlay pass uploads as it is:
// - downscaled to #455's caps (2048² base colour, 1024² every other role; 1024² / 512² in the
//   simplified mode), keeping its aspect ratio, colour roles averaged in linear light;
// - mipmapped down to 1x1, so the renderer never builds mips on the GL thread;
// - encoded once to UASTC 4x4 with the Basis Universal encoder, then transcoded block by block to
//   ASTC 4x4 (every GLES 3.2 / Android Extension Pack phone) and to ETC2 (the GLES 3.0 floor, for
//   the few that lack ASTC), so the renderer picks one by what the GPU has and never encodes.
//
// The simplified mode does not get a second copy: it starts the same mip chain lower
// (`lowDetailBaseLevel`, GL_TEXTURE_BASE_LEVEL), which is free and keeps both tiers the same image.
//
// Texture memory per model (32 MB flagship, 8 MB simplified) is enforced by fitting, the way the
// triangle cap is: while over, the texture with the largest top level gives that level up (the
// flagship) or starts one level lower (the simplified mode). A model is refused for texture memory
// only if every texture is down to its last level and it is still over, which eight materials
// cannot reach.
//
// Pure: no GL, no files, no threads (the Basis encoder's job pool is never started). The cost is
// the UASTC encode, a few seconds for a full 2048² chain on a phone, so it belongs on the import
// worker, never the GL thread.
namespace geode::scene::object {

// What a texture does in its material. The role decides the cap and the colour space: base colour
// and emissive are sRGB (averaged in linear light, sampled through an sRGB format), the rest are data.
// Ordered by precedence: an image used in two roles is made for the first.
enum class TextureRole : uint8_t { BaseColour, Emissive, Normal, MetallicRoughness, Occlusion };

enum class ImageCodec : uint8_t { Unknown, Png, Jpeg, Ktx2 };

struct TextureTierLimits {
    uint32_t baseColourSize;  // the longest side of a base colour texture
    uint32_t otherSize;       // the longest side of every other role
    std::size_t budgetBytes;  // GPU memory of all the model's textures in this tier, mips included
};

struct TextureLimits {
    TextureTierLimits flagship{2048, 1024, 32u * 1024u * 1024u};
    TextureTierLimits simplified{1024, 512, 8u * 1024u * 1024u};
    // The largest image the import will decode at all (64 MB as RGBA8). Past it the texture is
    // dropped with a warning instead of risking the import worker's memory on one picture; a KTX2
    // with mips is judged by the level it is read from, not its top.
    uint64_t maxSourcePixels = 4096ull * 4096ull;
};

// One mip level, in both encodings. ASTC 4x4 is 16 bytes a block; ETC2 is 16 bytes a block with
// alpha (GL_COMPRESSED_RGBA8_ETC2_EAC) and 8 without (GL_COMPRESSED_RGB8_ETC2, ETC1-compatible).
struct TextureLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> astc;
    std::vector<uint8_t> etc2;
};

struct ModelTexture {
    TextureRole role = TextureRole::BaseColour;
    ImageCodec source = ImageCodec::Unknown;
    bool srgb = false;      // upload as the SRGB8_ALPHA8 / SRGB8 format variants
    bool hasAlpha = false;  // base colour only: some texel is not opaque (ETC2 then carries EAC alpha)
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    std::vector<TextureLevel> levels;  // level 0 is the flagship's top, down to 1x1
    uint32_t lowDetailBaseLevel = 0;   // where the simplified mode's chain starts

    bool downscaled() const;
    // GPU bytes of one tier's chain, counted as ASTC (never smaller than the ETC2 copy).
    std::size_t gpuBytes(bool lowDetail) const;
};

// One image a model's materials use, before it is made. `bytes` stays the caller's.
struct TextureSource {
    const uint8_t* bytes = nullptr;
    std::size_t size = 0;
    TextureRole role = TextureRole::BaseColour;
};

struct TextureDrop {
    std::size_t source = 0;
    std::string why;  // for the import sheet and the log ("not a PNG, JPEG or KTX2 image")
};

struct TextureBuild {
    std::vector<ModelTexture> textures;
    std::vector<int> textureOfSource;  // per source: its index in `textures`, -1 when dropped
    std::vector<TextureDrop> drops;
    bool fittedToBudget = false;       // some texture gave up levels for the memory budget
    bool overBudget = false;           // still over after every texture reached its last level
    std::size_t bytesBeforeFit = 0;    // flagship bytes at the role caps, before the budget fit
    std::size_t bytesFlagship = 0;
    std::size_t bytesLowDetail = 0;
};

ImageCodec sniffImage(const uint8_t* bytes, std::size_t size);

// Makes every source into a GPU-ready texture within `limits`. Never throws.
TextureBuild buildTextures(const std::vector<TextureSource>& sources, const TextureLimits& limits = {});

}  // namespace geode::scene::object
