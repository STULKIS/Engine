// Frontier Engine - forward renderer: shadow pass, sky, terrain, instanced
// foliage, water, bloom and ACES tone mapping.
#pragma once

#include "asset/AssetLibrary.h"
#include "core/Config.h"
#include "render/Framebuffer.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "render/Texture.h"
#include "scene/Frustum.h"
#include "scene/Scene.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace frontier::scene { class Camera; }
namespace frontier::world { class World; }

namespace frontier::render {

struct RenderStats {
    int drawCalls = 0;
    size_t triangles = 0;
    int shadowDrawCalls = 0;
    size_t shadowTriangles = 0;
    int visibleInstances = 0;
    int culledInstances = 0;
    int sceneWidth = 0;
    int sceneHeight = 0;
    int msaaSamples = 0;
    int shadowMapSize = 0;
    float shadowPassMs = 0.0f;
    float mainPassMs = 0.0f;
    float postPassMs = 0.0f;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(const core::RenderConfig& config, const std::string& assetDirectory);
    void Shutdown();

    // Window framebuffer size in pixels; the HDR target may be scaled.
    void Resize(int width, int height);
    void Render(const scene::Camera& camera, world::World& world, scene::Scene& entities,
                const core::RenderConfig& config, float simulationTime);

    // Saves the current back buffer as a binary PPM (no image library needed).
    bool SaveScreenshot(const std::string& path) const;

    const RenderStats& Stats() const { return m_stats; }
    // Meshes and textures found on disk; the editor lists these.
    const asset::AssetLibrary& Assets() const { return m_assets; }
    int OutputWidth() const { return m_windowWidth; }
    int OutputHeight() const { return m_windowHeight; }
    bool Ready() const { return m_ready; }

private:
    struct Frame {
        const scene::Camera* camera = nullptr;
        world::World* world = nullptr;
        scene::Scene* entities = nullptr;
        core::RenderConfig settings;
        float time = 0.0f;
        mat4 viewProj{1.0f};
        mat4 view{1.0f};
        mat4 lightSpace{1.0f};      // near cascade
        mat4 lightSpaceFar{1.0f};   // far cascade
        float cascadeSplit = 60.0f; // world units where the far cascade takes over
        vec3 cameraPos{0.0f};
        float aspect = 1.0f;
        int sceneWidth = 0;
        int sceneHeight = 0;
    };

    bool LoadPrograms(const std::string& assetDirectory);
    bool CreateTextures();
    bool CreateTargets(int width, int height);
    bool RecreateSceneTarget(int width, int height);

    void UpdateTargets();
    void RenderShadowPass();
    void RenderSkyPass();
    void RenderTerrainPass();
    void RenderFoliagePass();
    void RenderObjectPass();
    void RenderAssetPass();
    void RenderWaterPass();
    bool CreatePrimitiveMeshes();
    // Generates missing placeholder art, scans the asset folder and uploads it.
    bool LoadAssets(const std::string& assetDirectory);
    // Groups scene entities by primitive into per-instance buffers.
    void BuildInstanceBuffers();
    void RenderBloomPass();
    void RenderPostPass();

    void SetAtmosphereUniforms(Shader& shader);
    void SetShadowUniforms(Shader& shader);
    void BindTexture(int unit, const Texture& texture) const;

    Shader m_skyShader;
    Shader m_terrainShader;
    Shader m_foliageShader;
    Shader m_waterShader;
    Shader m_shadowShader;
    Shader m_shadowInstancedShader;
    Shader m_objectShader;
    Shader m_assetShader;
    Shader m_shadowAssetShader;
    Shader m_bloomThresholdShader;
    Shader m_bloomBlurShader;
    Shader m_postShader;

    Texture m_grassTexture;
    Texture m_rockTexture;
    Texture m_soilTexture;

    Mesh m_fullscreenTriangle;
    // One mesh and one instance buffer per built-in primitive.
    Mesh m_primitiveMeshes[static_cast<int>(scene::Primitive::Count)];
    std::vector<std::vector<InstanceData>> m_primitiveInstances;
    std::vector<int> m_primitiveDrawCounts;

    // Imported assets, uploaded lazily on the first frame that needs them.
    asset::AssetLibrary m_assets;
    std::unordered_map<std::string, Mesh> m_assetMeshes;
    std::unordered_map<std::string, Texture> m_assetTextures;
    SceneTarget m_sceneTarget;
    ShadowMap m_shadowMap;
    BloomChain m_bloom;

    core::RenderConfig m_config;
    std::string m_assetDirectory;
    Frame m_frame;
    RenderStats m_stats;

    int m_windowWidth = 1600;
    int m_windowHeight = 900;
    // Last *requested* scene target settings (as opposed to the effective ones
    // stored in SceneTarget after driver clamping) so the renderer can tell a
    // real configuration change from a clamped value.
    int m_sceneRequestSamples = -1;
    bool m_sceneRequestHdr = false;
    int m_shadowRequestedSize = -1;
    bool m_ready = false;
};

} // namespace frontier::render
