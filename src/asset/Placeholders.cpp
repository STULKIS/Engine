// Derecho Engine - procedural placeholder models and hand-painted textures.
#include "asset/Placeholders.h"

#include "asset/Image.h"
#include "asset/Mesh.h"
#include "core/Log.h"

#include <cmath>
#include <filesystem>
#include <random>

#include <glm/gtc/constants.hpp>

namespace derecho::asset {

namespace {

namespace fs = std::filesystem;

fs::path Directory(const std::string& root, const char* leaf) {
    return std::filesystem::path(root) / leaf;
}

// ------------------------------------------------------------------ textures

float Hash(int x, int y, int seed) {
    int h = x * 374761393 + y * 668265263 + seed * 1274126177;
    h = (h ^ (h >> 13)) * 1274126177;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

float SmoothNoise(float x, float y, int seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Hash(x0, y0, seed);
    const float b = Hash(x0 + 1, y0, seed);
    const float c = Hash(x0, y0 + 1, seed);
    const float d = Hash(x0 + 1, y0 + 1, seed);
    return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sy);
}

float Fbm(float x, float y, int seed, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value += SmoothNoise(x * frequency, y * frequency, seed + i * 31) * amplitude;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

vec3 Rgb(int r, int g, int b) {
    return vec3(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)) / 255.0f;
}

// Cloth: cool teal base, vertical folds, a warm cream trim band and baked-in
// soft shading so it reads as painted rather than flat-shaded.
Image PaintCloth() {
    constexpr int kSize = 256;
    Image image;
    image.width = kSize;
    image.height = kSize;
    image.pixels.assign(static_cast<size_t>(kSize) * kSize * 4u, 255);

    const vec3 deep = Rgb(28, 62, 84);
    const vec3 mid = Rgb(58, 116, 138);
    const vec3 light = Rgb(126, 186, 194);
    const vec3 trim = Rgb(226, 196, 138);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(kSize);
            const float v = static_cast<float>(y) / static_cast<float>(kSize);

            // Folds: slow sine plus noise so the bands are not perfectly regular.
            const float fold = std::sin((u * 7.0f + Fbm(u * 3.0f, v * 3.0f, 11) * 0.6f) *
                                        glm::two_pi<float>());
            const float shade = 0.5f + fold * 0.28f + (0.5f - v) * 0.25f;
            vec3 color = glm::mix(deep, light, glm::clamp(shade, 0.0f, 1.0f));
            color = glm::mix(color, mid, 0.25f);

            // Painted grain and a few brighter brush streaks.
            const float grain = Fbm(u * 24.0f, v * 24.0f, 23);
            color *= 0.92f + grain * 0.16f;
            const float streak = SmoothNoise(u * 40.0f, v * 5.0f, 41);
            color += vec3(0.04f) * std::max(0.0f, streak - 0.6f) * 2.5f;

            // A painted hem line, offset from the edges so box-mapped props do
            // not repeat a border on every face.
            if (v > 0.44f && v < 0.47f) {
                color = glm::mix(trim, color, 0.35f + grain * 0.1f);
            }

            uint8_t* pixel = image.At(x, y);
            pixel[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixel[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixel[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixel[3] = 255;
        }
    }
    return image;
}

// Stone: warm grey with mossy patches, cracks and a painted light/fold ramp.
Image PaintStone() {
    constexpr int kSize = 256;
    Image image;
    image.width = kSize;
    image.height = kSize;
    image.pixels.assign(static_cast<size_t>(kSize) * kSize * 4u, 255);

    const vec3 cold = Rgb(74, 78, 82);
    const vec3 warm = Rgb(168, 158, 142);
    const vec3 moss = Rgb(96, 118, 74);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(kSize);
            const float v = static_cast<float>(y) / static_cast<float>(kSize);

            const float base = Fbm(u * 5.0f, v * 5.0f, 5);
            vec3 color = glm::mix(cold, warm, glm::clamp(base * 1.25f, 0.0f, 1.0f));

            // Cracks: thin dark ridges from a second noise field.
            const float crack = std::abs(Fbm(u * 9.0f, v * 9.0f, 17) - 0.5f);
            if (crack < 0.035f) {
                color *= 0.55f + crack * 4.0f;
            }

            // Moss grows towards the top of the texture and in damp patches.
            const float mossMask = Fbm(u * 6.0f, v * 6.0f, 29) * (0.4f + v * 0.8f);
            if (mossMask > 0.62f) {
                color = glm::mix(color, moss, glm::clamp((mossMask - 0.62f) * 3.5f, 0.0f, 0.7f));
            }

            // Painted lighting ramp: lighter at the top, softly banded.
            const float band = std::floor((0.5f - v) * 4.0f + 0.5f) / 4.0f;
            color *= 1.0f + band * 0.35f;

            // A few speckles of lichen.
            if (Hash(x / 2, y / 2, 71) > 0.985f) {
                color = glm::mix(color, Rgb(206, 210, 176), 0.55f);
            }

            uint8_t* pixel = image.At(x, y);
            pixel[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixel[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixel[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixel[3] = 255;
        }
    }
    return image;
}

// Skin for the character: warm base with a soft blush and a painted rim.
Image PaintSkin() {
    constexpr int kSize = 128;
    Image image;
    image.width = kSize;
    image.height = kSize;
    image.pixels.assign(static_cast<size_t>(kSize) * kSize * 4u, 255);

    const vec3 base = Rgb(238, 202, 178);
    const vec3 blush = Rgb(226, 152, 140);
    const vec3 shadow = Rgb(186, 138, 132);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) / static_cast<float>(kSize)) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) / static_cast<float>(kSize)) * 2.0f - 1.0f;
            const float radius = std::sqrt(u * u + v * v);

            vec3 color = glm::mix(shadow, base, glm::smoothstep(0.35f, 1.15f, radius + 0.35f));
            const float blushMask = std::max(0.0f, 1.0f - std::abs(u) * 1.4f) *
                                    std::max(0.0f, 1.0f - std::abs(v - 0.25f) * 2.2f);
            color = glm::mix(color, blush, blushMask * 0.65f);
            color *= 0.96f + Fbm(u * 6.0f + 4.0f, v * 6.0f + 4.0f, 3) * 0.08f;

            uint8_t* pixel = image.At(x, y);
            pixel[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixel[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixel[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixel[3] = 255;
        }
    }
    return image;
}

// -------------------------------------------------------------------- models

// A stylised low-poly traveller, roughly 1.8 m tall, standing on y = 0.
MeshData MakeCharacter() {
    MeshData mesh;
    // Legs.
    AppendBox(mesh, vec3(-0.16f, 0.42f, 0.0f), vec3(0.11f, 0.42f, 0.13f));
    AppendBox(mesh, vec3(0.16f, 0.42f, 0.0f), vec3(0.11f, 0.42f, 0.13f));
    // Boots.
    AppendBox(mesh, vec3(-0.16f, 0.06f, 0.04f), vec3(0.13f, 0.06f, 0.20f));
    AppendBox(mesh, vec3(0.16f, 0.06f, 0.04f), vec3(0.13f, 0.06f, 0.20f));
    // Hips and torso, slightly tapered by stacking two boxes.
    AppendBox(mesh, vec3(0.0f, 0.92f, 0.0f), vec3(0.26f, 0.10f, 0.17f));
    AppendBox(mesh, vec3(0.0f, 1.18f, 0.0f), vec3(0.29f, 0.17f, 0.19f));
    AppendBox(mesh, vec3(0.0f, 1.40f, 0.0f), vec3(0.33f, 0.10f, 0.21f));
    // Arms, angled slightly out from the shoulders.
    AppendBox(mesh, vec3(-0.42f, 1.20f, 0.0f), vec3(0.09f, 0.26f, 0.10f));
    AppendBox(mesh, vec3(0.42f, 1.20f, 0.0f), vec3(0.09f, 0.26f, 0.10f));
    // Hands.
    AppendBox(mesh, vec3(-0.44f, 0.90f, 0.0f), vec3(0.08f, 0.09f, 0.09f));
    AppendBox(mesh, vec3(0.44f, 0.90f, 0.0f), vec3(0.08f, 0.09f, 0.09f));
    // Neck, head, hair cap and a scarf for a readable silhouette.
    AppendBox(mesh, vec3(0.0f, 1.55f, 0.0f), vec3(0.08f, 0.06f, 0.08f));
    AppendBox(mesh, vec3(0.0f, 1.72f, 0.0f), vec3(0.14f, 0.14f, 0.14f));
    AppendBox(mesh, vec3(0.0f, 1.83f, -0.01f), vec3(0.16f, 0.06f, 0.16f));
    AppendBox(mesh, vec3(0.0f, 1.48f, 0.0f), vec3(0.24f, 0.05f, 0.20f));

    // Gentle forward lean of the upper body would be nice, but keeping the model
    // axis-aligned keeps the placeholder readable and cheap.
    mesh.ComputeBounds();
    return mesh;
}

// A faceted crystal: hexagonal column with pyramidal caps.
MeshData MakeCrystal() {
    MeshData mesh;
    constexpr int kSides = 6;
    const float radius = 0.35f;
    const float bodyBottom = 0.35f;
    const float bodyTop = 1.15f;
    const vec3 tip(0.0f, 1.6f, 0.0f);
    const vec3 baseTip(0.0f, -0.1f, 0.0f);

    std::vector<uint32_t> bottomRing;
    std::vector<uint32_t> topRing;
    for (int i = 0; i < kSides; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(kSides) * glm::two_pi<float>();
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;

        Vertex v;
        v.position = vec3(x, bodyBottom, z);
        v.normal = vec3(x, 0.0f, z);
        v.uv = vec2(static_cast<float>(i) / kSides, 1.0f);
        bottomRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(v);

        v.position = vec3(x, bodyTop, z);
        v.uv = vec2(static_cast<float>(i) / kSides, 0.2f);
        topRing.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(v);
    }
    for (int i = 0; i < kSides; ++i) {
        const int n = (i + 1) % kSides;
        mesh.indices.insert(mesh.indices.end(),
                            {bottomRing[i], topRing[i], bottomRing[n],
                             bottomRing[n], topRing[i], topRing[n]});
    }

    const uint32_t tipIndex = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{tip, vec3(0, 1, 0), vec2(0.5f, 0.0f)});
    const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{baseTip, vec3(0, -1, 0), vec2(0.5f, 1.0f)});
    for (int i = 0; i < kSides; ++i) {
        const int n = (i + 1) % kSides;
        mesh.indices.insert(mesh.indices.end(), {topRing[i], tipIndex, topRing[n]});
        mesh.indices.insert(mesh.indices.end(), {bottomRing[n], baseIndex, bottomRing[i]});
    }

    mesh.ComputeBounds();
    return mesh;
}

// A lumpy boulder built from a deformed icosphere.
MeshData MakeRock() {
    MeshData mesh;
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> jitter(-0.16f, 0.16f);

    // Icosphere by subdividing an icosahedron twice (320 triangles).
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<vec3> positions = {
        glm::normalize(vec3(-1, t, 0)), glm::normalize(vec3(1, t, 0)),
        glm::normalize(vec3(-1, -t, 0)), glm::normalize(vec3(1, -t, 0)),
        glm::normalize(vec3(0, -1, t)), glm::normalize(vec3(0, 1, t)),
        glm::normalize(vec3(0, -1, -t)), glm::normalize(vec3(0, 1, -t)),
        glm::normalize(vec3(t, 0, -1)), glm::normalize(vec3(t, 0, 1)),
        glm::normalize(vec3(-t, 0, -1)), glm::normalize(vec3(-t, 0, 1)),
    };
    std::vector<uint32_t> triangles = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
                                       1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                                       3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
                                       4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};

    for (int subdivision = 0; subdivision < 1; ++subdivision) {
        std::vector<uint32_t> next;
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const uint32_t a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            const uint32_t ab = static_cast<uint32_t>(positions.size());
            positions.push_back(glm::normalize(positions[a] + positions[b]));
            const uint32_t bc = static_cast<uint32_t>(positions.size());
            positions.push_back(glm::normalize(positions[b] + positions[c]));
            const uint32_t ca = static_cast<uint32_t>(positions.size());
            positions.push_back(glm::normalize(positions[c] + positions[a]));
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        triangles.swap(next);
    }

    for (const vec3& direction : positions) {
        const float radius = 1.0f + jitter(rng);
        Vertex vertex;
        vertex.position = direction * radius;
        vertex.position.y *= 0.72f; // squat boulder
        vertex.normal = direction;
        vertex.uv = vec2(direction.x * 0.5f + 0.5f, direction.z * 0.5f + 0.5f);
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = triangles;

    // Flat-shade the low-poly rock so the facets read.
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Vertex& a = mesh.vertices[mesh.indices[i]];
        Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const vec3 normal = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
        a.normal = b.normal = c.normal = normal;
    }
    mesh.ComputeBounds();
    return mesh;
}

bool WriteMesh(const fs::path& path, const MeshData& mesh, const std::string& name,
               std::string& error) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return true;
    }
    fs::create_directories(path.parent_path(), ec);
    if (!SaveObj(path.string(), mesh, name, error)) {
        return false;
    }
    DE_INFO("Placeholder model written: %s (%zu tris)", path.string().c_str(),
            mesh.indices.size() / 3);
    return true;
}

bool WriteTexture(const fs::path& path, const Image& image, const std::string& errorLabel,
                  std::string& error) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return true;
    }
    fs::create_directories(path.parent_path(), ec);
    const bool isTga = path.extension() == ".tga";
    const bool ok = isTga ? SaveTga(image, path.string(), error) : SavePng(image, path.string(), error);
    if (!ok) {
        error = errorLabel + ": " + error;
        return false;
    }
    DE_INFO("Placeholder texture written: %s", path.string().c_str());
    return true;
}

} // namespace

bool EnsurePlaceholderAssets(const std::string& root, std::string& error) {
    error.clear();
    const fs::path models = Directory(root, "models");
    const fs::path textures = Directory(root, "textures");

    if (!WriteMesh(models / "character.obj", MakeCharacter(), "character", error)) {
        return false;
    }
    if (!WriteMesh(models / "crystal.obj", MakeCrystal(), "crystal", error)) {
        return false;
    }
    if (!WriteMesh(models / "rock.obj", MakeRock(), "rock", error)) {
        return false;
    }
    if (!WriteTexture(textures / "cloth.png", PaintCloth(), "cloth", error)) {
        return false;
    }
    if (!WriteTexture(textures / "stone.tga", PaintStone(), "stone", error)) {
        return false;
    }
    if (!WriteTexture(textures / "skin.png", PaintSkin(), "skin", error)) {
        return false;
    }
    return true;
}

} // namespace derecho::asset
