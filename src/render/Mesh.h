// Derecho Engine - GPU mesh with optional per-instance transform data.
#pragma once

#include "math/Math.h"
#include "render/GL.h"
#include "scene/Frustum.h"

#include <cstdint>
#include <vector>

namespace derecho::render {

struct Vertex {
    vec3 position{0.0f};
    vec3 normal{0.0f, 1.0f, 0.0f};
    vec2 uv{0.0f};
    // RGBA vertex tint. Alpha stores the wind-sway weight for foliage.
    vec4 color{1.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    scene::AABB bounds{vec3(0.0f), vec3(0.0f)};

    void RecomputeBounds();
    size_t TriangleCount() const { return indices.size() / 3; }
};

// Per-instance payload: transform plus tint/wind phase.
struct InstanceData {
    mat4 transform{1.0f};
    vec4 tint{1.0f}; // rgb = colour multiplier, a = wind phase in radians
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    bool Create(const MeshData& data, bool dynamic = false);
    void Destroy();

    void Draw() const;
    void DrawInstanced(int instanceCount) const;

    bool EnsureInstanceCapacity(int instanceCount);
    void UploadInstances(const std::vector<InstanceData>& instances);
    int InstanceCapacity() const { return m_instanceCapacity; }

    size_t IndexCount() const { return m_indexCount; }
    size_t VertexCount() const { return m_vertexCount; }
    size_t TriangleCount() const { return m_indexCount / 3; }
    const scene::AABB& Bounds() const { return m_bounds; }
    bool Valid() const { return m_vao != 0; }

private:
    void SetupVertexLayout();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_instanceVbo = 0;
    int m_instanceCapacity = 0;
    size_t m_indexCount = 0;
    size_t m_vertexCount = 0;
    scene::AABB m_bounds{vec3(0.0f), vec3(0.0f)};
};

} // namespace derecho::render
