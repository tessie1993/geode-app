// Host-side checks for importing the user's .glb models (core/scene/object/GltfImport, #474). Built
// and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler,
// against the third_party/cgltf and third_party/meshoptimizer submodules; no GL, no device.
//
// Every fixture is a .glb built here, JSON and BIN chunk in code, so nothing binary is committed and
// each fixture says exactly what it trips. What is pinned is #455's limit table and import rules as
// a caller sees them: the verdict, the reasons, the warnings and the before/after counts.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "meshoptimizer.h"
#include "scene/object/GltfImport.hpp"

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

// A .glb assembled piece by piece. Accessors and views index into one BIN chunk (buffer 0).
struct Glb {
    std::vector<uint8_t> bin;
    std::vector<std::string> buffers;  // extra buffers after buffer 0
    std::vector<std::string> views, accessors, meshes, nodes, materials, skins;
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

}  // namespace

int main() {
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
    if (failures > 0) {
        std::printf("%d gltf import check(s) failed\n", failures);
        return 1;
    }
    std::printf("gltf import: all checks passed\n");
    return 0;
}
