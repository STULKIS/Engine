// Frontier Engine - procedural lake surface with wind-driven Gerstner waves.
#pragma once

#include "render/Mesh.h"

namespace frontier::scene {

class Terrain;

class Water {
public:
    Water() = default;

    void Configure(float level, float size, int resolution = 128);
    bool BuildRenderResources(const Terrain& terrain);
    void ReleaseRenderResources();

    float Level() const { return m_level; }
    float Size() const { return m_size; }
    int Resolution() const { return m_resolution; }
    render::Mesh& Mesh() { return m_mesh; }
    const render::Mesh& Mesh() const { return m_mesh; }
    bool Valid() const { return m_mesh.Valid(); }

private:
    float m_level = 0.0f;
    float m_size = 900.0f;
    int m_resolution = 128;
    render::Mesh m_mesh;
};

} // namespace frontier::scene
