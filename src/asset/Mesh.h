// Derecho Engine - mesh data and Wavefront OBJ import/export.
//
// OBJ is the lowest-friction way to get real art into the engine: it is text,
// it is exported by every DCC tool, and it needs no SDK. glTF is on the roadmap;
// this covers the same "bring your own model" need today.
#pragma once

#include "math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace derecho::asset {

struct Vertex {
    vec3 position{0.0f};
    vec3 normal{0.0f, 1.0f, 0.0f};
    vec2 uv{0.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vec3 boundsMin{0.0f};
    vec3 boundsMax{0.0f};

    bool Empty() const { return vertices.empty() || indices.empty(); }
    void ComputeBounds();
};

// Reads v/vt/vn/f records, triangulates polygons, supports negative indices and
// generates smooth normals when the file has none.
bool LoadObj(const std::string& path, MeshData& out, std::string& error);

// Writes positions, normals, UVs and triangles (used to author placeholder art).
bool SaveObj(const std::string& path, const MeshData& mesh, const std::string& name,
             std::string& error);

// Helpers used by the placeholder generator.
void AppendBox(MeshData& mesh, const vec3& centre, const vec3& halfExtents);
void AppendCylinder(MeshData& mesh, const vec3& base, float radius, float height, int segments);

} // namespace derecho::asset
