// Frontier Engine - OBJ import/export and mesh helpers.
#include "asset/Mesh.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/constants.hpp>

namespace frontier::asset {

namespace {

// Key for welding shared position/normal/uv triples that differ only in index.
struct VertexKey {
    int position = 0;
    int uv = 0;
    int normal = 0;

    bool operator==(const VertexKey& other) const {
        return position == other.position && uv == other.uv && normal == other.normal;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const {
        size_t hash = static_cast<size_t>(key.position) * 73856093u;
        hash ^= static_cast<size_t>(key.uv) * 19349663u;
        hash ^= static_cast<size_t>(key.normal) * 83492791u;
        return hash;
    }
};

int ResolveIndex(int value, size_t count) {
    if (value > 0) {
        return value - 1;
    }
    if (value < 0) {
        return static_cast<int>(count) + value;
    }
    return -1;
}

void ParseFaceVertex(const std::string& token, int& position, int& uv, int& normal) {
    position = 0;
    uv = 0;
    normal = 0;
    int field = 0;
    std::string current;
    for (size_t i = 0; i <= token.size(); ++i) {
        const char c = i < token.size() ? token[i] : '/';
        if (c == '/' || i == token.size()) {
            const int value = current.empty() ? 0 : std::atoi(current.c_str());
            if (field == 0) position = value;
            else if (field == 1) uv = value;
            else if (field == 2) normal = value;
            ++field;
            current.clear();
            if (i == token.size()) {
                break;
            }
            continue;
        }
        current.push_back(c);
    }
}

} // namespace

void MeshData::ComputeBounds() {
    if (vertices.empty()) {
        boundsMin = boundsMax = vec3(0.0f);
        return;
    }
    boundsMin = boundsMax = vertices.front().position;
    for (const Vertex& vertex : vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
}

bool LoadObj(const std::string& path, MeshData& out, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "could not open '" + path + "'";
        return false;
    }

    std::vector<vec3> positions;
    std::vector<vec2> uvs;
    std::vector<vec3> normals;
    out = MeshData();
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> lookup;
    bool hasNormals = false;

    std::string line;
    int lineNumber = 0;
    std::string token;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream(line);
        stream >> token;
        if (token == "v") {
            vec3 position(0.0f);
            stream >> position.x >> position.y >> position.z;
            positions.push_back(position);
        } else if (token == "vt") {
            vec2 uv(0.0f);
            stream >> uv.x >> uv.y;
            // OBJ has an upward V axis; renderers (and our images) go down.
            uvs.push_back(vec2(uv.x, 1.0f - uv.y));
        } else if (token == "vn") {
            vec3 normal(0.0f);
            stream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (token == "f") {
            std::vector<VertexKey> face;
            while (stream >> token) {
                int position = 0, uv = 0, normal = 0;
                ParseFaceVertex(token, position, uv, normal);
                VertexKey key;
                key.position = ResolveIndex(position, positions.size());
                key.uv = uv != 0 ? ResolveIndex(uv, uvs.size()) : -1;
                key.normal = normal != 0 ? ResolveIndex(normal, normals.size()) : -1;
                if (key.position < 0 || static_cast<size_t>(key.position) >= positions.size()) {
                    error = "OBJ: face references a missing vertex on line " +
                            std::to_string(lineNumber);
                    return false;
                }
                face.push_back(key);
            }
            if (face.size() < 3) {
                continue;
            }
            for (size_t i = 2; i < face.size(); ++i) {
                const VertexKey triangle[3] = {face[0], face[i - 1], face[i]};
                for (const VertexKey& key : triangle) {
                    auto found = lookup.find(key);
                    if (found != lookup.end()) {
                        out.indices.push_back(found->second);
                        continue;
                    }
                    Vertex vertex;
                    vertex.position = positions[static_cast<size_t>(key.position)];
                    if (key.uv >= 0 && static_cast<size_t>(key.uv) < uvs.size()) {
                        vertex.uv = uvs[static_cast<size_t>(key.uv)];
                    }
                    if (key.normal >= 0 && static_cast<size_t>(key.normal) < normals.size()) {
                        vertex.normal = normals[static_cast<size_t>(key.normal)];
                        hasNormals = true;
                    } else {
                        vertex.normal = vec3(0.0f);
                    }
                    const uint32_t index = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(vertex);
                    lookup.emplace(key, index);
                    out.indices.push_back(index);
                }
            }
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        error = "'" + path + "' contains no triangles";
        return false;
    }

    if (!hasNormals) {
        // Area-weighted smooth normals, since flat shading would fight the cel look.
        for (uint32_t index : out.indices) {
            (void)index;
        }
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            Vertex& a = out.vertices[out.indices[i]];
            Vertex& b = out.vertices[out.indices[i + 1]];
            Vertex& c = out.vertices[out.indices[i + 2]];
            const vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
            a.normal += faceNormal;
            b.normal += faceNormal;
            c.normal += faceNormal;
        }
        for (Vertex& vertex : out.vertices) {
            const float length = glm::length(vertex.normal);
            vertex.normal = length > 1e-6f ? vertex.normal / length : vec3(0.0f, 1.0f, 0.0f);
        }
    }

    out.ComputeBounds();
    return true;
}

bool SaveObj(const std::string& path, const MeshData& mesh, const std::string& name,
             std::string& error) {
    std::ofstream file(path);
    if (!file) {
        error = "could not write '" + path + "'";
        return false;
    }
    file << "# Frontier Engine placeholder asset\n";
    file << "# " << name << "\n";
    file << "o " << name << "\n";
    file.setf(std::ios::fixed);
    file.precision(4);
    for (const Vertex& vertex : mesh.vertices) {
        file << "v " << vertex.position.x << ' ' << vertex.position.y << ' ' << vertex.position.z
             << '\n';
    }
    for (const Vertex& vertex : mesh.vertices) {
        file << "vt " << vertex.uv.x << ' ' << (1.0f - vertex.uv.y) << '\n';
    }
    for (const Vertex& vertex : mesh.vertices) {
        file << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z
             << '\n';
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        file << "f";
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t index = mesh.indices[i + static_cast<size_t>(corner)] + 1;
            file << ' ' << index << '/' << index << '/' << index;
        }
        file << '\n';
    }
    return static_cast<bool>(file);
}

void AppendBox(MeshData& mesh, const vec3& centre, const vec3& halfExtents) {
    const vec3 corners[8] = {
        centre + vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
        centre + vec3(halfExtents.x, -halfExtents.y, -halfExtents.z),
        centre + vec3(halfExtents.x, halfExtents.y, -halfExtents.z),
        centre + vec3(-halfExtents.x, halfExtents.y, -halfExtents.z),
        centre + vec3(-halfExtents.x, -halfExtents.y, halfExtents.z),
        centre + vec3(halfExtents.x, -halfExtents.y, halfExtents.z),
        centre + vec3(halfExtents.x, halfExtents.y, halfExtents.z),
        centre + vec3(-halfExtents.x, halfExtents.y, halfExtents.z),
    };
    const int faces[6][4] = {
        {4, 5, 6, 7}, // +Z
        {1, 0, 3, 2}, // -Z
        {5, 1, 2, 6}, // +X
        {0, 4, 7, 3}, // -X
        {7, 6, 2, 3}, // +Y
        {0, 1, 5, 4}, // -Y
    };
    const vec3 faceNormals[6] = {
        vec3(0, 0, 1), vec3(0, 0, -1), vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0),
    };
    const vec2 faceUVs[4] = {vec2(0, 1), vec2(1, 1), vec2(1, 0), vec2(0, 0)};
    for (int face = 0; face < 6; ++face) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int corner = 0; corner < 4; ++corner) {
            Vertex vertex;
            vertex.position = corners[faces[face][corner]];
            vertex.normal = faceNormals[face];
            vertex.uv = faceUVs[corner];
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
}

void AppendCylinder(MeshData& mesh, const vec3& base, float radius, float height, int segments) {
    if (segments < 3) {
        segments = 3;
    }
    const uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) *
                            glm::two_pi<float>();
        const float x = std::cos(angle);
        const float z = std::sin(angle);
        Vertex bottom;
        bottom.position = base + vec3(x * radius, 0.0f, z * radius);
        bottom.normal = vec3(x, 0.0f, z);
        bottom.uv = vec2(static_cast<float>(i) / static_cast<float>(segments), 1.0f);
        mesh.vertices.push_back(bottom);
        Vertex top = bottom;
        top.position = base + vec3(x * radius, height, z * radius);
        top.uv = vec2(static_cast<float>(i) / static_cast<float>(segments), 0.0f);
        mesh.vertices.push_back(top);
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = start + static_cast<uint32_t>(i) * 2u;
        const uint32_t b = a + 1u;
        const uint32_t c = a + 2u;
        const uint32_t d = a + 3u;
        mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
    }
}

} // namespace frontier::asset
