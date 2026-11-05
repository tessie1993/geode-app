// Host-side checks for importing the user's .glb models (core/scene/object/GltfImport, #474). Built
// and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler,
// against the third_party/cgltf, third_party/meshoptimizer and third_party/basis_universal
// submodules; no GL, no device.
//
// Every fixture is a .glb built here, JSON and BIN chunk in code, so nothing binary is committed and
// each fixture says exactly what it trips. What is pinned is #455's limit table and import rules as
// a caller sees them: the verdict, the reasons, the warnings and the before/after counts. Texture
// fixtures are built here too: PNGs through the miniz Basis ships, a KTX2 assembled from UASTC
// blocks, and one small JPEG kept as bytes (the only codec with no encoder at hand). Made textures
// are checked by decoding their ASTC and ETC2 blocks back with Basis's own block decoders.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "encoder/basisu_enc.h"
#include "encoder/basisu_gpu_texture.h"
#include "encoder/basisu_uastc_enc.h"
#include "meshoptimizer.h"
#include "scene/object/GltfImport.hpp"
#include "transcoder/basisu_transcoder.h"

// The miniz in Basis (its implementation is compiled in basisu_enc.cpp): the PNG writer for fixtures.
#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "encoder/basisu_miniz.h"

using namespace geode::scene::object;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out;
}

std::string quoted(const std::vector<std::string>& names) {
    std::vector<std::string> q;
    for (const auto& n : names) q.push_back("\"" + n + "\"");
    return join(q);
}

// A gently rolling grid of n x n quads (2n² triangles): curved, so the simplifier has real error to
// weigh instead of folding a flat sheet down to two triangles.
struct Grid {
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

Grid grid(int n) {
    Grid g;
    for (int y = 0; y <= n; ++y) {
        for (int x = 0; x <= n; ++x) {
            const float u = static_cast<float>(x) / n, v = static_cast<float>(y) / n;
            g.positions.insert(g.positions.end(), {u, v, 0.08f * std::sin(9.0f * u) * std::cos(7.0f * v)});
        }
    }
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const uint32_t a = y * (n + 1) + x, b = a + 1, c = a + (n + 1), d = c + 1;
            g.indices.insert(g.indices.end(), {a, c, b, b, c, d});
        }
    }
    return g;
}

// A .glb assembled piece by piece. Accessors and views index into one BIN chunk (buffer 0).
struct Glb {
    std::vector<uint8_t> bin;
    std::vector<std::string> buffers;  // extra buffers after buffer 0
    std::vector<std::string> views, accessors, meshes, nodes, materials, skins, images, textures, samplers;
    std::vector<int> sceneNodes;
    std::vector<std::string> used, required;
    std::string bufferUri;  // non-empty: buffer 0 points outside the file

    int addView(const void* data, std::size_t bytes, int stride = 0) {
        while (bin.size() % 4) bin.push_back(0);
        const std::size_t offset = bin.size();
        const auto* p = static_cast<const uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        std::string v = "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(bytes);
        if (stride) v += ",\"byteStride\":" + std::to_string(stride);
        views.push_back(v + "}");
        return static_cast<int>(views.size() - 1);
    }

    int addAccessor(int view, int componentType, std::size_t count, const char* type) {
        accessors.push_back("{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" + std::to_string(componentType) +
                            ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"}");
        return static_cast<int>(accessors.size() - 1);
    }

    int addPositions(const std::vector<float>& xyz) {
        return addAccessor(addView(xyz.data(), xyz.size() * 4), 5126, xyz.size() / 3, "VEC3");
    }

    int addIndices(const std::vector<uint32_t>& idx) {
        return addAccessor(addView(idx.data(), idx.size() * 4), 5125, idx.size(), "SCALAR");
    }

    int addMesh(const std::string& primitive) {
        meshes.push_back("{\"primitives\":[" + primitive + "]}");
        return static_cast<int>(meshes.size() - 1);
    }

    int addMaterial() {
        materials.push_back("{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1]}}");
        return static_cast<int>(materials.size() - 1);
    }

    int addImage(const std::vector<uint8_t>& bytes, const char* mime) {
        const int view = addView(bytes.data(), bytes.size());
        images.push_back("{\"bufferView\":" + std::to_string(view) + ",\"mimeType\":\"" + mime + "\"}");
        return static_cast<int>(images.size() - 1);
    }

    int addTexture(int image, int sampler = -1) {
        std::string t = "{\"source\":" + std::to_string(image);
        if (sampler >= 0) t += ",\"sampler\":" + std::to_string(sampler);
        textures.push_back(t + "}");
        return static_cast<int>(textures.size() - 1);
    }

    // `body` is the material's JSON members, e.g. a pbrMetallicRoughness with a baseColorTexture.
    int addMaterial(const std::string& body) {
        materials.push_back("{" + body + "}");
        return static_cast<int>(materials.size() - 1);
    }

    // A small grid mesh drawn with `material`, placed once.
    void addGridWith(int material) {
        const Grid g = grid(2);
        const int pos = addPositions(g.positions);
        const int idx = addIndices(g.indices);
        const int mesh = addMesh("{\"attributes\":{\"POSITION\":" + std::to_string(pos) + "},\"indices\":" +
                                 std::to_string(idx) + ",\"material\":" + std::to_string(material) + "}");
        addNode("\"mesh\":" + std::to_string(mesh));
    }

    int addNode(const std::string& body, bool inScene = true) {
        nodes.push_back("{" + body + "}");
        const int index = static_cast<int>(nodes.size() - 1);
        if (inScene) sceneNodes.push_back(index);
        return index;
    }

    std::vector<uint8_t> build() const {
        std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[";
        std::vector<std::string> sn;
        for (int n : sceneNodes) sn.push_back(std::to_string(n));
        json += join(sn) + "]}]";
        if (!nodes.empty()) json += ",\"nodes\":[" + join(nodes) + "]";
        if (!meshes.empty()) json += ",\"meshes\":[" + join(meshes) + "]";
        if (!materials.empty()) json += ",\"materials\":[" + join(materials) + "]";
        if (!skins.empty()) json += ",\"skins\":[" + join(skins) + "]";
        if (!images.empty()) json += ",\"images\":[" + join(images) + "]";
        if (!textures.empty()) json += ",\"textures\":[" + join(textures) + "]";
        if (!samplers.empty()) json += ",\"samplers\":[" + join(samplers) + "]";
        std::string buffer0 = "{\"byteLength\":" + std::to_string(bin.size());
        if (!bufferUri.empty()) buffer0 += ",\"uri\":\"" + bufferUri + "\"";
        std::vector<std::string> allBuffers{buffer0 + "}"};
        allBuffers.insert(allBuffers.end(), buffers.begin(), buffers.end());
        json += ",\"buffers\":[" + join(allBuffers) + "]";
        if (!views.empty()) json += ",\"bufferViews\":[" + join(views) + "]";
        if (!accessors.empty()) json += ",\"accessors\":[" + join(accessors) + "]";
        if (!used.empty()) json += ",\"extensionsUsed\":[" + quoted(used) + "]";
        if (!required.empty()) json += ",\"extensionsRequired\":[" + quoted(required) + "]";
        json += "}";
        while (json.size() % 4) json += ' ';
        std::vector<uint8_t> chunk = bin;
        while (chunk.size() % 4) chunk.push_back(0);

        std::vector<uint8_t> out;
        auto u32 = [&out](uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        };
        u32(0x46546C67u);  // "glTF"
        u32(2);
        u32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + chunk.size()));
        u32(static_cast<uint32_t>(json.size()));
        u32(0x4E4F534Au);  // JSON
        out.insert(out.end(), json.begin(), json.end());
        u32(static_cast<uint32_t>(chunk.size()));
        u32(0x004E4942u);  // BIN
        out.insert(out.end(), chunk.begin(), chunk.end());
        return out;
    }
};

std::string trianglesPrimitive(int positions, int indices, int material = -1, const std::string& extra = "") {
    std::string p = "{\"attributes\":{\"POSITION\":" + std::to_string(positions) + "},\"indices\":" + std::to_string(indices);
    if (material >= 0) p += ",\"material\":" + std::to_string(material);
    return p + extra + "}";
}

// One grid mesh, placed by `instances` nodes, with an optional material.
Glb gridModel(int n, int instances = 1, bool withMaterial = false) {
    Glb glb;
    const Grid g = grid(n);
    const int pos = glb.addPositions(g.positions);
    const int idx = glb.addIndices(g.indices);
    const int mesh = glb.addMesh(trianglesPrimitive(pos, idx, withMaterial ? glb.addMaterial() : -1));
    for (int i = 0; i < instances; ++i) {
        glb.addNode("\"mesh\":" + std::to_string(mesh) + ",\"translation\":[" + std::to_string(i * 2) + ",0,0]");
    }
    return glb;
}

ImportResult run(const Glb& glb, const ImportLimits& limits = {}) {
    const std::vector<uint8_t> bytes = glb.build();
    return importGlb(bytes.data(), bytes.size(), limits);
}

// Each triangle rotated to start at its smallest index: the meshopt index codec may rotate a
// triangle's corners (winding kept), so decoded lists are compared up to that.
std::vector<uint32_t> canonical(std::vector<uint32_t> idx) {
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        while (idx[t] > idx[t + 1] || idx[t] > idx[t + 2]) {
            const uint32_t first = idx[t];
            idx[t] = idx[t + 1];
            idx[t + 1] = idx[t + 2];
            idx[t + 2] = first;
        }
    }
    return idx;
}

bool indicesInRange(const ImportResult& r) {
    for (const ModelMesh& m : r.meshes) {
        for (uint32_t i : m.indices) if (i >= m.vertexCount()) return false;
        for (uint32_t i : m.lowDetailIndices) if (i >= m.vertexCount()) return false;
    }
    return true;
}

void limitsMatchTheTable() {
    const ImportLimits l;
    check(l.fileBudgetBytes == 15u * 1024 * 1024 && l.fileRefuseBytes == 50u * 1024 * 1024, "15 MB budget, refused over 50 MB");
    check(l.maxFitRatio == 4.0f, "the automatic fit tries up to 4x over");
    check(l.flagship.maxTriangles == 50000 && l.simplified.maxTriangles == 15000, "50k / 15k triangles per model");
    check(l.flagship.maxMaterials == 8 && l.simplified.maxMaterials == 4, "8 / 4 materials per model");
    check(l.flagship.maxJoints == 128 && l.simplified.maxJoints == 64, "128 / 64 joints");
    check(l.flagship.maxMorphTargets == 8 && l.simplified.maxMorphTargets == 0, "8 morph targets, off in simplified");
}

void smallModelImportsUntouched() {
    const ImportResult r = run(gridModel(10, 1, true));
    check(r.ok && r.refusals.empty(), "a small model imports");
    check(r.warnings.empty(), "with no warnings");
    check(!r.fitted, "and is not decimated");
    check(r.counts.trianglesSource == 200 && r.counts.trianglesFlagship == 200 && r.counts.trianglesLowDetail == 200,
          "every detail level keeps all 200 triangles when already under both caps");
    check(r.counts.meshes == 1 && r.counts.instances == 1 && r.counts.materials == 1, "one mesh, one instance, one material");
    check(r.meshes.size() == 1 && r.meshes[0].vertexCount() == 121 && r.meshes[0].material == 0, "the mesh's vertices and material");
    check(triangleCount(r, false) == 200 && triangleCount(r, true) == 200, "triangleCount agrees with the counts");
}

void notAGlbIsRefused() {
    const std::string text = "{\"asset\":{\"version\":\"2.0\"}}";
    ImportResult r = importGlb(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    check(!r.ok && r.refusedFor(Refusal::NotGlb), "a .gltf JSON file is not a .glb");
    const uint8_t tiny[4] = {'g', 'l', 'T', 'F'};
    r = importGlb(tiny, sizeof(tiny));
    check(r.refusedFor(Refusal::NotGlb), "four bytes are not a .glb");
    std::vector<uint8_t> legacy = gridModel(2).build();
    legacy[4] = 1;  // container version 1
    r = importGlb(legacy.data(), legacy.size());
    check(r.refusedFor(Refusal::NotGlb), "a glTF 1.0 container is refused as not a .glb");
}

void fileSizeLimits() {
    const ImportLimits defaults;
    std::vector<uint8_t> huge(defaults.fileRefuseBytes + 1, 0);
    ImportResult r = importGlb(huge.data(), huge.size());
    check(r.refusedFor(Refusal::TooLarge) && r.refusals.size() == 1, "over 50 MB is refused before parsing");

    ImportLimits small;
    small.fileBudgetBytes = 1024;  // the 10x10 grid is a few KB
    r = run(gridModel(10), small);
    check(r.ok && r.warned(Warning::OverFileBudget), "over the file budget imports with a warning");
}

void malformedIsRefused() {
    std::vector<uint8_t> bytes = gridModel(10).build();
    bytes.resize(bytes.size() - 64);
    ImportResult r = importGlb(bytes.data(), bytes.size());
    check(r.refusedFor(Refusal::Malformed), "a truncated file is malformed");

    Glb bad;
    const Grid g = grid(2);
    std::vector<uint32_t> idx = g.indices;
    idx[4] = 9999;  // past the last vertex
    const int pos = bad.addPositions(g.positions);
    bad.addNode("\"mesh\":" + std::to_string(bad.addMesh(trianglesPrimitive(pos, bad.addIndices(idx)))));
    r = run(bad);
    check(r.refusedFor(Refusal::Malformed) && r.meshes.empty(), "an index past the last vertex is malformed");

    Glb external = gridModel(2);
    external.bufferUri = "model.bin";
    r = run(external);
    check(r.refusedFor(Refusal::ExternalBuffer), "a buffer outside the file is refused");
}

void extensionRules() {
    Glb unknown = gridModel(4);
    unknown.used = unknown.required = {"EXT_not_a_real_extension"};
    ImportResult r = run(unknown);
    check(!r.ok && r.refusedFor(Refusal::UnknownRequiredExtension) && r.refusals[0].detail == "EXT_not_a_real_extension",
          "an unknown required extension is refused, by name");

    Glb accepted = gridModel(4);
    accepted.used = accepted.required = {"KHR_mesh_quantization", "KHR_texture_transform"};
    accepted.used.push_back("KHR_materials_unlit");
    r = run(accepted);
    check(r.ok && r.warnings.empty(), "accepted extensions, required or not, import silently");

    Glb ignored = gridModel(4);
    ignored.used = {"KHR_materials_clearcoat", "KHR_lights_punctual", "EXT_some_vendor_thing"};
    r = run(ignored);
    int ignoredCount = 0;
    for (const auto& w : r.warnings) ignoredCount += w.code == Warning::IgnoredExtension;
    check(r.ok && ignoredCount == 2, "heavy materials and punctual lights are ignored with a warning");
    check(r.warned(Warning::UnknownOptionalExtension), "an unknown optional extension is skipped with a warning");

    Glb dracoRequired = gridModel(4);
    dracoRequired.used = dracoRequired.required = {"KHR_draco_mesh_compression"};
    r = run(dracoRequired);
    check(r.refusedFor(Refusal::DracoUnsupported), "required Draco is refused until it can be re-encoded");

    Glb dracoOptional = gridModel(4);
    dracoOptional.used = {"KHR_draco_mesh_compression"};
    r = run(dracoOptional);
    check(r.ok && r.warned(Warning::DracoFallback), "optional Draco imports the uncompressed fallback");
}

void decimatesJustOverTheCap() {
    const ImportResult r = run(gridModel(160));  // 51,200 triangles
    check(r.ok && r.fitted, "a model just over 50k is fitted, not refused");
    check(r.counts.trianglesSource == 51200, "the before count is the source's");
    check(r.counts.trianglesFlagship <= 50000 && r.counts.trianglesFlagship >= 40000,
          "the after count is under the cap without gutting the mesh");
    check(r.counts.trianglesLowDetail <= 15000 && r.counts.trianglesLowDetail >= 10000, "the low-detail copy is under 15k");
    check(triangleCount(r, false) == r.counts.trianglesFlagship && triangleCount(r, true) == r.counts.trianglesLowDetail,
          "the index lists draw what the counts say");
    check(indicesInRange(r), "every simplified index is a real vertex");
}

void decimatesUpToFourTimesOver() {
    const ImportResult r = run(gridModel(310));  // 192,200 triangles, 3.8x over
    check(r.ok && r.fitted, "3.8x over is still fitted");
    check(r.counts.trianglesSource == 192200 && r.counts.trianglesFlagship <= 50000 && r.counts.trianglesFlagship >= 40000,
          "before and after counts across a 3.8x fit");
    check(r.counts.trianglesLowDetail <= 15000, "and its low-detail copy fits the simplified cap");
    check(indicesInRange(r), "every index of the big fit is a real vertex");
}

void refusesBeyondFourTimesOver() {
    const ImportResult r = run(gridModel(320));  // 204,800 triangles, 4.1x over
    check(!r.ok && r.refusedFor(Refusal::TooManyTriangles), "past 4x over is refused without trying");
    check(r.counts.trianglesSource == 204800, "the refusal still reports the count");
    check(r.meshes.empty() && r.instances.empty(), "a refusal hands back no geometry");
}

void instancesCountTowardTheCap() {
    const ImportResult r = run(gridModel(100, 3));  // 20,000 triangles placed three times
    check(r.ok && r.fitted, "three placements of a 20k mesh are 60k drawn, so the model is fitted");
    check(r.counts.meshes == 1 && r.counts.instances == 3 && r.meshes.size() == 1, "the mesh is stored once, placed three times");
    check(r.counts.trianglesSource == 60000 && triangleCount(r, false) <= 50000, "instances count toward the cap");
    check(r.instances.size() == 3 && r.instances[2].world[12] == 4.0f, "each placement keeps its node's world matrix");
}

void materialLimits() {
    auto withMaterials = [](int count) {
        Glb glb;
        const Grid g = grid(2);
        for (int i = 0; i < count; ++i) {
            const int pos = glb.addPositions(g.positions);
            const int mesh = glb.addMesh(trianglesPrimitive(pos, glb.addIndices(g.indices), glb.addMaterial()));
            glb.addNode("\"mesh\":" + std::to_string(mesh));
        }
        return glb;
    };
    ImportResult r = run(withMaterials(9));
    check(!r.ok && r.refusedFor(Refusal::TooManyMaterials), "nine materials are refused");
    r = run(withMaterials(5));
    check(r.ok && r.counts.materials == 5 && r.warned(Warning::SimplifiedMaterials),
          "five materials import, warned for the simplified mode's four");
}

void skinLimits() {
    auto withJoints = [](int joints) {
        Glb glb = gridModel(4);
        glb.nodes[0] = "{\"mesh\":0,\"skin\":0}";
        std::vector<std::string> ids;
        for (int i = 0; i < joints; ++i) ids.push_back(std::to_string(glb.addNode("\"name\":\"j\"", false)));
        glb.skins.push_back("{\"joints\":[" + join(ids) + "]}");
        return glb;
    };
    ImportResult r = run(withJoints(129));
    check(!r.ok && r.refusedFor(Refusal::TooManyJoints), "129 joints are refused");
    r = run(withJoints(100));
    check(r.ok && r.counts.maxJoints == 100 && r.warned(Warning::SimplifiedJoints), "100 joints import, warned past 64");
}

void morphLimits() {
    auto withTargets = [](int targets) {
        Glb glb;
        const Grid g = grid(4);
        const int pos = glb.addPositions(g.positions);
        std::vector<std::string> t(targets, "{\"POSITION\":" + std::to_string(pos) + "}");
        glb.addNode("\"mesh\":" + std::to_string(glb.addMesh(trianglesPrimitive(pos, glb.addIndices(g.indices), -1,
                                                                                ",\"targets\":[" + join(t) + "]"))));
        return glb;
    };
    ImportResult r = run(withTargets(9));
    check(!r.ok && r.refusedFor(Refusal::TooManyMorphTargets), "nine morph targets are refused");
    r = run(withTargets(2));
    check(r.ok && r.counts.maxMorphTargets == 2 && r.warned(Warning::SimplifiedMorphTargetsOff),
          "two morph targets import, warned that the simplified mode draws none");
}

void topologies() {
    Glb lines;
    const Grid g = grid(2);
    const int pos = lines.addPositions(g.positions);
    lines.addNode("\"mesh\":" + std::to_string(lines.addMesh(trianglesPrimitive(pos, lines.addIndices(g.indices), -1, ",\"mode\":1"))));
    ImportResult r = run(lines);
    check(!r.ok && r.refusedFor(Refusal::NoGeometry) && r.warned(Warning::NonTrianglePrimitiveSkipped),
          "a lines-only file has no geometry, and says lines were skipped");

    Glb strip;
    const std::vector<float> p{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0, 0, 2, 0};
    const int sp = strip.addPositions(p);
    strip.addNode("\"mesh\":" + std::to_string(strip.addMesh("{\"attributes\":{\"POSITION\":" + std::to_string(sp) + "},\"mode\":5}")));
    r = run(strip);
    check(r.ok && r.counts.trianglesSource == 3 && r.meshes[0].indices.size() == 9, "a five-vertex strip is three triangles");
    check(r.ok && r.meshes[0].indices[3] == 2 && r.meshes[0].indices[4] == 1 && r.meshes[0].indices[5] == 3,
          "odd strip triangles keep their winding");
}

void meshoptCompressionDecodes() {
    const Grid g = grid(20);
    const std::size_t vertices = g.positions.size() / 3;
    std::vector<unsigned char> vtx(meshopt_encodeVertexBufferBound(vertices, 12));
    vtx.resize(meshopt_encodeVertexBuffer(vtx.data(), vtx.size(), g.positions.data(), vertices, 12));
    std::vector<unsigned char> idx(meshopt_encodeIndexBufferBound(g.indices.size(), vertices));
    idx.resize(meshopt_encodeIndexBuffer(idx.data(), idx.size(), g.indices.data(), g.indices.size()));

    Glb glb;
    // Compressed bytes live in buffer 0 (BIN); the views point at buffer 1, the fallback with no data.
    auto compressed = [&glb](const std::vector<unsigned char>& data, std::size_t stride, std::size_t count, const char* mode) {
        while (glb.bin.size() % 4) glb.bin.push_back(0);
        const std::size_t offset = glb.bin.size();
        glb.bin.insert(glb.bin.end(), data.begin(), data.end());
        glb.views.push_back("{\"buffer\":1,\"byteOffset\":0,\"byteLength\":" + std::to_string(stride * count) +
                            (std::strcmp(mode, "ATTRIBUTES") == 0 ? ",\"byteStride\":" + std::to_string(stride) : std::string()) +
                            ",\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
                            ",\"byteLength\":" + std::to_string(data.size()) + ",\"byteStride\":" + std::to_string(stride) +
                            ",\"count\":" + std::to_string(count) + ",\"mode\":\"" + mode + "\"}}}");
        return static_cast<int>(glb.views.size() - 1);
    };
    const int pos = glb.addAccessor(compressed(vtx, 12, vertices, "ATTRIBUTES"), 5126, vertices, "VEC3");
    const int ind = glb.addAccessor(compressed(idx, 4, g.indices.size(), "TRIANGLES"), 5125, g.indices.size(), "SCALAR");
    glb.buffers.push_back("{\"byteLength\":" + std::to_string(vertices * 12 + g.indices.size() * 4) +
                          ",\"extensions\":{\"EXT_meshopt_compression\":{\"fallback\":true}}}");
    glb.addNode("\"mesh\":" + std::to_string(glb.addMesh(trianglesPrimitive(pos, ind))));
    glb.used = glb.required = {"EXT_meshopt_compression"};

    const ImportResult r = run(glb);
    check(r.ok && r.warnings.empty(), "a meshopt-compressed model imports");
    check(r.ok && r.counts.trianglesSource == 800, "its triangles are counted from the decoded indices");
    check(r.ok && r.meshes[0].positions == g.positions && canonical(r.meshes[0].indices) == canonical(g.indices),
          "and decode to exactly the source");
}

// ---- Textures (#474 part 2) ----

using Texel = std::array<uint8_t, 4>;

std::vector<uint8_t> png(uint32_t w, uint32_t h, const std::function<Texel(uint32_t, uint32_t)>& texel) {
    std::vector<uint8_t> px(std::size_t{w} * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) std::memcpy(&px[(std::size_t{y} * w + x) * 4], texel(x, y).data(), 4);
    }
    std::size_t len = 0;
    void* data = buminiz::tdefl_write_image_to_png_file_in_memory(px.data(), static_cast<int>(w), static_cast<int>(h), 4, &len);
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> out(bytes, bytes + len);
    buminiz::mz_free(data);
    return out;
}

std::vector<uint8_t> solidPng(uint32_t w, uint32_t h, Texel c) {
    return png(w, h, [c](uint32_t, uint32_t) { return c; });
}

// A 24x16 baseline JPEG of one colour, (200, 60, 30) before compression; decoders read it back as
// about (199, 60, 29). Written once by an image library, kept as bytes.
const uint8_t kJpeg[] = {
    0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0xff,0xdb,0x00,0x43,
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0xff,0xdb,0x00,0x43,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0xff,0xc0,0x00,0x11,0x08,0x00,0x10,0x00,0x18,0x03,
    0x01,0x11,0x00,0x02,0x11,0x01,0x03,0x11,0x01,0xff,0xc4,0x00,0x1f,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x10,0x00,
    0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7d,0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,
    0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,
    0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,
    0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,
    0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,
    0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,
    0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xc4,0x00,0x1f,0x01,0x00,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x11,0x00,
    0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,
    0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,
    0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,
    0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,
    0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,
    0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,
    0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0xf8,
    0x3e,0xbf,0xcc,0x73,0xfe,0x90,0x02,0x80,0x0a,0x00,0x28,0x00,0xa0,0x02,0x80,0x3f,0xff,0xd9,
};

// A KTX2 of UASTC 4x4 blocks with no supercompression, one solid colour, `levels` mips from w x h:
// the file KHR_texture_basisu carries, assembled field by field (KTX2 spec 3.x, Basis's DFD).
std::vector<uint8_t> uastcKtx2(uint32_t w, uint32_t h, uint32_t levels, Texel c) {
    std::vector<std::vector<uint8_t>> data(levels);
    for (uint32_t l = 0; l < levels; ++l) {
        const uint32_t lw = std::max<uint32_t>(1, w >> l), lh = std::max<uint32_t>(1, h >> l);
        const uint32_t blocks = ((lw + 3) / 4) * ((lh + 3) / 4);
        uint8_t texels[64];
        for (int i = 0; i < 16; ++i) std::memcpy(&texels[i * 4], c.data(), 4);
        basist::uastc_block block;
        basisu::encode_uastc(texels, block, basisu::cPackUASTCLevelFastest);
        for (uint32_t b = 0; b < blocks; ++b) {
            const auto* p = reinterpret_cast<const uint8_t*>(&block);
            data[l].insert(data[l].end(), p, p + 16);
        }
    }
    std::vector<uint8_t> out;
    auto u32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    auto u64 = [&u32](uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    };
    const uint8_t id[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), id, id + 12);
    const uint32_t dfdOffset = 80 + 24 * levels;
    u32(0);  // vkFormat: VK_FORMAT_UNDEFINED, the DFD names UASTC
    u32(1);  // typeSize
    u32(w);
    u32(h);
    u32(0);  // depth
    u32(0);  // layers
    u32(1);  // faces
    u32(levels);
    u32(0);  // no supercompression
    u32(dfdOffset);
    u32(44);
    u32(0);  // no key/value data
    u32(0);
    u64(0);  // no supercompression global data
    u64(0);
    std::size_t at = (dfdOffset + 44 + 15) / 16 * 16;
    std::vector<std::size_t> offsets(levels);
    for (uint32_t l = 0; l < levels; ++l) {
        offsets[l] = at;
        at += (data[l].size() + 15) / 16 * 16;
    }
    for (uint32_t l = 0; l < levels; ++l) {
        u64(offsets[l]);
        u64(data[l].size());
        u64(data[l].size());
    }
    u32(44);                       // DFD total size
    u32(0);                        // vendor 0 (Khronos), descriptor type 0
    u32(2u | (40u << 16));         // version 2, block size 40
    u32(166u | (1u << 8) | (2u << 16));  // colour model UASTC, BT.709, sRGB transfer
    u32(0x0303u);                  // 4x4 texel blocks
    u32(16);                       // bytes per plane
    u32(0);
    u32((127u << 16) | (0u << 24));  // one 128-bit sample, channel RGB
    u32(0);
    u32(0);
    u32(0xFFFFFFFFu);
    for (uint32_t l = 0; l < levels; ++l) {
        out.resize(offsets[l], 0);
        out.insert(out.end(), data[l].begin(), data[l].end());
    }
    return out;
}

Texel astcTexel(const TextureLevel& level, uint32_t bx, uint32_t by) {
    const uint32_t blocksX = (level.width + 3) / 4;
    basisu::color_rgba px[16];
    basisu::unpack_block(basisu::texture_format::cASTC_LDR_4x4, &level.astc[(std::size_t{by} * blocksX + bx) * 16], px, false);
    return {px[0].r, px[0].g, px[0].b, px[0].a};
}

Texel etcTexel(const TextureLevel& level, bool alpha) {
    basisu::color_rgba px[16];
    basisu::unpack_block(alpha ? basisu::texture_format::cETC2_RGBA : basisu::texture_format::cETC1, level.etc2.data(), px, false);
    return {px[0].r, px[0].g, px[0].b, px[0].a};
}

bool near(const Texel& a, const Texel& b, int tolerance, int channels = 3) {
    for (int c = 0; c < channels; ++c) {
        if (std::abs(int{a[c]} - int{b[c]}) > tolerance) return false;
    }
    return true;
}

std::size_t chainBytes(uint32_t w, uint32_t h) {
    std::size_t total = 0;
    for (;;) {
        total += std::size_t{(w + 3) / 4} * ((h + 3) / 4) * 16;
        if (w == 1 && h == 1) return total;
        w = std::max<uint32_t>(1, w / 2);
        h = std::max<uint32_t>(1, h / 2);
    }
}

std::string textureInfo(int texture) { return "{\"index\":" + std::to_string(texture) + "}"; }

std::string baseColourMaterial(int texture) {
    return "\"pbrMetallicRoughness\":{\"baseColorTexture\":" + textureInfo(texture) + "}";
}

void textureLimitsMatchTheTable() {
    const TextureLimits t = ImportLimits{}.textures;
    check(t.flagship.baseColourSize == 2048 && t.flagship.otherSize == 1024, "2048² base colour, 1024² others");
    check(t.simplified.baseColourSize == 1024 && t.simplified.otherSize == 512, "1024² / 512² in the simplified mode");
    check(t.flagship.budgetBytes == 32u * 1024 * 1024 && t.simplified.budgetBytes == 8u * 1024 * 1024,
          "32 MB / 8 MB texture memory per model");
}

void pngBaseColourIsMade() {
    Glb glb;
    const Texel colour{10, 200, 90, 255};
    glb.addGridWith(glb.addMaterial(baseColourMaterial(glb.addTexture(glb.addImage(solidPng(64, 32, colour), "image/png")))));
    const ImportResult r = run(glb);
    check(r.ok && r.warnings.empty(), "a model with a PNG base colour imports silently");
    check(r.textures.size() == 1 && r.counts.textures == 1 && r.counts.texturesDropped == 0, "one texture made");
    check(r.materials.size() == 1 && r.materials[0].baseColourTexture.texture == 0 && r.meshes[0].material == 0,
          "the mesh's material reads it as base colour");
    if (r.textures.size() != 1) return;
    const ModelTexture& t = r.textures[0];
    check(t.role == TextureRole::BaseColour && t.srgb && !t.hasAlpha && t.source == ImageCodec::Png, "an opaque sRGB PNG");
    check(t.sourceWidth == 64 && t.sourceHeight == 32 && !t.downscaled(), "under the caps it keeps its size");
    check(t.levels.size() == 7 && t.levels[0].width == 64 && t.levels[0].height == 32 && t.levels[6].width == 1 &&
              t.levels[6].height == 1,
          "a full mip chain, 64x32 down to 1x1");
    check(t.levels[1].width == 32 && t.levels[1].height == 16 && t.levels[5].width == 2 && t.levels[5].height == 1,
          "each level halves, never below 1");
    check(t.levels[0].astc.size() == 16 * 8 * 16 && t.levels[0].etc2.size() == 16 * 8 * 8,
          "ASTC 4x4 is 16 bytes a block, opaque ETC2 8");
    check(t.lowDetailBaseLevel == 0, "under the simplified cap the simplified mode starts at the top");
    check(near(astcTexel(t.levels[0], 3, 2), colour, 3), "the ASTC blocks decode to the source colour");
    check(near(etcTexel(t.levels[0], false), colour, 8), "and so do the ETC2 blocks");
    check(near(astcTexel(t.levels[6], 0, 0), colour, 3), "the 1x1 mip keeps the colour");
    check(t.gpuBytes(false) == chainBytes(64, 32) && r.counts.textureBytesFlagship == t.gpuBytes(false) &&
              r.counts.textureBytesLowDetail == t.gpuBytes(true),
          "texture memory is the ASTC chain, as the counts say");
}

void downscalesToTheRoleCaps() {
    ImportLimits limits;
    limits.textures.flagship = {64, 32, 1u << 30};
    limits.textures.simplified = {32, 16, 1u << 30};
    Glb glb;
    // One-texel black and white checks: an exact 2:1 shrink averages two of each, mid grey in linear
    // light, which is 188 in sRGB (128 would be the mistake of averaging the stored values).
    const auto checker = png(128, 64, [](uint32_t x, uint32_t y) {
        const uint8_t v = (x + y) % 2 ? 255 : 0;
        return Texel{v, v, v, 255};
    });
    const int colourTex = glb.addTexture(glb.addImage(checker, "image/png"));
    const int normalTex = glb.addTexture(glb.addImage(solidPng(128, 128, {128, 128, 255, 255}), "image/png"));
    glb.addGridWith(glb.addMaterial(baseColourMaterial(colourTex) + ",\"normalTexture\":" + textureInfo(normalTex)));
    const ImportResult r = run(glb, limits);
    check(r.ok && r.textures.size() == 2 && r.counts.texturesDownscaled == 2, "both textures are made smaller");
    if (r.textures.size() != 2) return;
    const ModelTexture& colour = r.textures[r.materials[0].baseColourTexture.texture];
    const ModelTexture& normal = r.textures[r.materials[0].normalTexture.texture];
    check(colour.levels[0].width == 64 && colour.levels[0].height == 32, "base colour to its cap, aspect kept");
    check(normal.levels[0].width == 32 && normal.levels[0].height == 32, "the normal map to the others' cap");
    check(!normal.srgb && normal.role == TextureRole::Normal, "a normal map is data, not sRGB");
    check(colour.lowDetailBaseLevel == 1 && normal.lowDetailBaseLevel == 1, "the simplified mode starts a level lower");
    check(near(astcTexel(colour.levels[0], 5, 3), {188, 188, 188, 255}, 6), "colour is averaged in linear light");
    check(near(astcTexel(normal.levels[0], 2, 2), {128, 128, 255, 255}, 4), "a flat normal stays flat");
}

void alphaKeepsItsChannel() {
    Glb glb;
    const Texel colour{240, 40, 40, 128};
    glb.addGridWith(glb.addMaterial(baseColourMaterial(glb.addTexture(glb.addImage(solidPng(16, 16, colour), "image/png"))) +
                                    ",\"alphaMode\":\"BLEND\""));
    const ImportResult r = run(glb);
    check(r.ok && r.textures.size() == 1 && r.textures[0].hasAlpha, "a translucent base colour is marked as such");
    check(r.ok && r.materials[0].alphaMode == AlphaMode::Blend, "and its material blends");
    if (r.textures.size() != 1) return;
    const TextureLevel& top = r.textures[0].levels[0];
    check(top.etc2.size() == 16 * 16, "ETC2 then carries an EAC alpha block, 16 bytes a block");
    check(near(astcTexel(top, 1, 1), colour, 4, 4), "ASTC keeps the alpha");
    check(near(etcTexel(top, true), colour, 10, 4), "ETC2 keeps the alpha");
}

void jpegIsMade() {
    Glb glb;
    const std::vector<uint8_t> jpeg(kJpeg, kJpeg + sizeof(kJpeg));
    glb.addGridWith(glb.addMaterial("\"emissiveFactor\":[1,1,1],\"emissiveTexture\":" +
                                    textureInfo(glb.addTexture(glb.addImage(jpeg, "image/jpeg")))));
    const ImportResult r = run(glb);
    check(r.ok && r.textures.size() == 1, "a JPEG emissive texture is made");
    if (r.textures.size() != 1) return;
    const ModelTexture& t = r.textures[0];
    check(t.source == ImageCodec::Jpeg && t.role == TextureRole::Emissive && t.srgb, "an sRGB emissive JPEG");
    check(t.levels[0].width == 24 && t.levels[0].height == 16 && t.levels.size() == 5, "24x16 with its mips");
    check(near(astcTexel(t.levels[0], 2, 1), {199, 60, 29, 255}, 6), "it decodes to its colour");
    check(r.materials[0].emissiveTexture.texture == 0 && r.materials[0].emissive[0] == 1.0f, "the emissive slot reads it");
}

void ktx2BasisuIsMade() {
    const Texel colour{30, 90, 220, 255};
    auto model = [&colour](uint32_t levels) {
        Glb glb;
        const int image = glb.addImage(uastcKtx2(16, 16, levels, colour), "image/ktx2");
        glb.textures.push_back("{\"extensions\":{\"KHR_texture_basisu\":{\"source\":" + std::to_string(image) + "}}}");
        glb.addGridWith(glb.addMaterial(baseColourMaterial(0)));
        glb.used = glb.required = {"KHR_texture_basisu"};
        return glb;
    };
    ImportResult r = run(model(1));
    check(r.ok && r.warnings.empty() && r.textures.size() == 1, "a KHR_texture_basisu KTX2 is made");
    if (r.textures.size() == 1) {
        const ModelTexture& t = r.textures[0];
        check(t.source == ImageCodec::Ktx2 && t.levels.size() == 5 && t.levels[0].width == 16, "16x16 with mips to 1x1");
        check(near(astcTexel(t.levels[0], 3, 3), colour, 4), "its UASTC reaches ASTC with the colour intact");
        check(near(etcTexel(t.levels[0], false), colour, 10), "and ETC2");
    }

    // A KTX2 that already has mips, under a smaller cap: read from the level that fits.
    ImportLimits limits;
    limits.textures.flagship.baseColourSize = 8;
    r = run(model(3), limits);
    check(r.ok && r.textures.size() == 1 && r.textures[0].levels[0].width == 8 && r.textures[0].downscaled(),
          "a KTX2 over the cap is made at the cap");
    if (r.textures.size() == 1) check(near(astcTexel(r.textures[0].levels[0], 1, 1), colour, 4), "with its colour");
}

void textureMemoryIsFitted() {
    ImportLimits limits;
    limits.textures.flagship = {256, 256, 30000};
    limits.textures.simplified = {256, 256, 8000};
    Glb glb;
    const int big = glb.addTexture(glb.addImage(solidPng(128, 128, {200, 0, 0, 255}), "image/png"));
    const int small = glb.addTexture(glb.addImage(solidPng(64, 64, {0, 200, 0, 255}), "image/png"));
    glb.addGridWith(glb.addMaterial(baseColourMaterial(big)));
    glb.addGridWith(glb.addMaterial(baseColourMaterial(small)));
    const ImportResult r = run(glb, limits);
    check(r.ok && r.warned(Warning::TexturesFitToBudget), "over the texture memory, the model is fitted, not refused");
    check(r.counts.textureBytesSource == chainBytes(128, 128) + chainBytes(64, 64), "the before count is at the role caps");
    check(r.counts.textureBytesFlagship <= 30000 && r.counts.textureBytesLowDetail <= 8000, "both tiers end under budget");
    if (r.textures.size() != 2) return;
    const ModelTexture& a = r.textures[r.materials[0].baseColourTexture.texture];
    const ModelTexture& b = r.textures[r.materials[1].baseColourTexture.texture];
    check(a.levels[0].width == 64 && b.levels[0].width == 64, "the largest texture gave up its top level");
    check(r.counts.texturesDownscaled == 1, "one texture is smaller than its source");
    check(r.counts.textureBytesFlagship == a.gpuBytes(false) + b.gpuBytes(false) &&
              r.counts.textureBytesLowDetail == a.gpuBytes(true) + b.gpuBytes(true),
          "the counts are the made chains");
    check(a.lowDetailBaseLevel + b.lowDetailBaseLevel >= 1, "the simplified mode starts lower to fit its budget");
}

void textureMemoryRefusal() {
    ImportLimits limits;
    limits.textures.flagship.budgetBytes = 8;  // less than one 1x1 level
    Glb glb;
    glb.addGridWith(glb.addMaterial(baseColourMaterial(glb.addTexture(glb.addImage(solidPng(8, 8, {1, 2, 3, 255}), "image/png")))));
    const ImportResult r = run(glb, limits);
    check(!r.ok && r.refusedFor(Refusal::TextureMemoryOverBudget), "a model that cannot fit its textures is refused");
    check(r.textures.empty() && r.materials.empty() && r.meshes.empty(), "and hands back nothing");
}

void unmakeableTexturesAreDropped() {
    ImportLimits limits;
    limits.textures.maxSourcePixels = 100;
    Glb glb;
    const std::string text = "not an image at all";
    const int junk = glb.addTexture(glb.addImage(std::vector<uint8_t>(text.begin(), text.end()), "image/png"));
    glb.images.push_back("{\"uri\":\"beside.png\"}");
    const int external = glb.addTexture(static_cast<int>(glb.images.size() - 1));
    const int huge = glb.addTexture(glb.addImage(solidPng(16, 16, {9, 9, 9, 255}), "image/png"));  // 256 > 100 pixels
    glb.addGridWith(glb.addMaterial(baseColourMaterial(junk) + ",\"emissiveTexture\":" + textureInfo(external) +
                                    ",\"occlusionTexture\":" + textureInfo(huge)));
    const ImportResult r = run(glb, limits);
    check(r.ok, "a model whose images cannot be made still imports");
    check(r.counts.texturesDropped == 3 && r.textures.empty(), "every unmakeable image is dropped");
    check(r.ok && r.materials[0].baseColourTexture.texture == -1 && r.materials[0].emissiveTexture.texture == -1 &&
              r.materials[0].occlusionTexture.texture == -1,
          "and its slots read no texture");
    bool named = false;
    for (const auto& w : r.warnings) named = named || (w.code == Warning::TextureDropped && w.detail.find("image 1") == 0);
    check(named, "each drop names its image");
}

void sharedImageIsMadeOnce() {
    Glb glb;
    glb.samplers.push_back("{\"wrapS\":33071,\"wrapT\":33648,\"magFilter\":9729,\"minFilter\":9987}");
    const int tex = glb.addTexture(glb.addImage(solidPng(16, 16, {50, 60, 70, 255}), "image/png"), 0);
    glb.addGridWith(glb.addMaterial(
        "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":" + std::to_string(tex) +
        ",\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.5,0.25],\"scale\":[2,2]}}}},"
        "\"occlusionTexture\":{\"index\":" + std::to_string(tex) + ",\"texCoord\":1,\"strength\":0.5},"
        "\"doubleSided\":true,\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,"
        "\"extensions\":{\"KHR_materials_unlit\":{},\"KHR_materials_emissive_strength\":{\"emissiveStrength\":4}}"));
    glb.used = {"KHR_texture_transform", "KHR_materials_unlit", "KHR_materials_emissive_strength"};
    const ImportResult r = run(glb);
    check(r.ok && r.textures.size() == 1, "an image in two roles is made once");
    if (r.textures.size() != 1 || r.materials.size() != 1) return;
    const ModelMaterial& m = r.materials[0];
    check(r.textures[0].role == TextureRole::BaseColour && r.textures[0].srgb, "for its most demanding role");
    check(m.baseColourTexture.texture == 0 && m.occlusionTexture.texture == 0, "both slots read it");
    check(m.baseColourTexture.wrapS == 33071 && m.baseColourTexture.wrapT == 33648 && m.baseColourTexture.minFilter == 9987,
          "the sampler is handed on");
    check(m.baseColourTexture.hasTransform && m.baseColourTexture.offset[0] == 0.5f && m.baseColourTexture.transformScale[1] == 2.0f,
          "and KHR_texture_transform");
    check(m.occlusionTexture.scale == 0.5f && m.occlusionTexture.texCoord == 1, "the occlusion strength and UV set");
    check(r.warned(Warning::TexCoordUnsupported), "a second UV set warns: only the first is imported");
    check(m.unlit && m.doubleSided && m.alphaMode == AlphaMode::Mask && m.alphaCutoff == 0.3f && m.emissiveStrength == 4.0f,
          "unlit, double-sided, mask cutoff and emissive strength are kept");
}

}  // namespace

int main() {
    basisu::basisu_encoder_init(false, false);  // the KTX2 fixture encodes UASTC before any import does
    limitsMatchTheTable();
    smallModelImportsUntouched();
    notAGlbIsRefused();
    fileSizeLimits();
    malformedIsRefused();
    extensionRules();
    decimatesJustOverTheCap();
    decimatesUpToFourTimesOver();
    refusesBeyondFourTimesOver();
    instancesCountTowardTheCap();
    materialLimits();
    skinLimits();
    morphLimits();
    topologies();
    meshoptCompressionDecodes();
    textureLimitsMatchTheTable();
    pngBaseColourIsMade();
    downscalesToTheRoleCaps();
    alphaKeepsItsChannel();
    jpegIsMade();
    ktx2BasisuIsMade();
    textureMemoryIsFitted();
    textureMemoryRefusal();
    unmakeableTexturesAreDropped();
    sharedImageIsMadeOnce();
    if (failures > 0) {
        std::printf("%d gltf import check(s) failed\n", failures);
        return 1;
    }
    std::printf("gltf import: all checks passed\n");
    return 0;
}
