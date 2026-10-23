#include "render/Mesh.h"

#include "core/Log.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace derecho::render {

void MeshData::RecomputeBounds() {
    if (vertices.empty()) {
        bounds = {vec3(0.0f), vec3(0.0f)};
        return;
    }
    vec3 lo = vertices.front().position;
    vec3 hi = vertices.front().position;
    for (const Vertex& v : vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    bounds = {lo, hi};
}

Mesh::~Mesh() {
    Destroy();
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo), m_instanceVbo(other.m_instanceVbo),
      m_instanceCapacity(other.m_instanceCapacity), m_indexCount(other.m_indexCount),
      m_vertexCount(other.m_vertexCount), m_bounds(other.m_bounds) {
    other.m_vao = other.m_vbo = other.m_ebo = other.m_instanceVbo = 0;
    other.m_instanceCapacity = 0;
    other.m_indexCount = other.m_vertexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_instanceVbo = other.m_instanceVbo;
        m_instanceCapacity = other.m_instanceCapacity;
        m_indexCount = other.m_indexCount;
        m_vertexCount = other.m_vertexCount;
        m_bounds = other.m_bounds;
        other.m_vao = other.m_vbo = other.m_ebo = other.m_instanceVbo = 0;
        other.m_instanceCapacity = 0;
        other.m_indexCount = other.m_vertexCount = 0;
    }
    return *this;
}

void Mesh::SetupVertexLayout() {
    glBindVertexArray(m_vao);

    // Per-vertex layout: position(3) normal(3) uv(2) colour(4)
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, color)));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBindVertexArray(0);
}

bool Mesh::Create(const MeshData& data, bool dynamic) {
    Destroy();
    if (data.vertices.empty() || data.indices.empty()) {
        DE_WARN("Mesh::Create called with empty geometry.");
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    m_indexCount = data.indices.size();
    m_vertexCount = data.vertices.size();
    m_bounds = data.bounds;

    SetupVertexLayout();
    return true;
}

void Mesh::Destroy() {
    if (m_instanceVbo) {
        glDeleteBuffers(1, &m_instanceVbo);
        m_instanceVbo = 0;
    }
    if (m_ebo) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_instanceCapacity = 0;
    m_indexCount = m_vertexCount = 0;
}

bool Mesh::EnsureInstanceCapacity(int instanceCount) {
    if (instanceCount <= m_instanceCapacity) {
        return true;
    }
    if (!m_vao) {
        return false;
    }
    const int newCapacity = std::max(instanceCount, std::max(64, m_instanceCapacity * 2));
    if (!m_instanceVbo) {
        glGenBuffers(1, &m_instanceVbo);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(InstanceData) * static_cast<size_t>(newCapacity)),
                 nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = sizeof(InstanceData);
    // Locations 4..7: transform columns (mat4), location 8: tint + wind phase.
    for (int column = 0; column < 4; ++column) {
        glEnableVertexAttribArray(4 + column);
        glVertexAttribPointer(4 + column, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(InstanceData, transform) + sizeof(vec4) * column));
        glVertexAttribDivisor(4 + column, 1);
    }
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(InstanceData, tint)));
    glVertexAttribDivisor(8, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_instanceCapacity = newCapacity;
    return true;
}

void Mesh::UploadInstances(const std::vector<InstanceData>& instances) {
    if (instances.empty() || !m_instanceVbo) {
        return;
    }
    if (!EnsureInstanceCapacity(static_cast<int>(instances.size()))) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceData)), instances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Mesh::Draw() const {
    if (!m_vao) {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::DrawInstanced(int instanceCount) const {
    if (!m_vao || instanceCount <= 0 || m_instanceCapacity <= 0) {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr,
                            instanceCount);
    glBindVertexArray(0);
}

} // namespace derecho::render
