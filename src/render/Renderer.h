// Frontier Engine - forward renderer: shadow pass, sky, terrain, instanced
// foliage, water, bloom and ACES tone mapping.
#pragma once

#include "core/Config.h"
#include "render/Framebuffer.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "render/Texture.h"
#include "scene/Frustum.h"

#include <string>
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
    void Render(const scene::Camera& camera, world::World& world, const core::RenderConfig& config,
                float simulationTime);

    // Saves the current back buffer as a binary PPM (no image library needed).
    bool SaveScreenshot(const std::string& path) const;

    const RenderStats& Stats() const { return m_stats; }
    int OutputWidth() const { return m_windowWidth; }
    int OutputHeight() const { return m_windowHeight; }
    bool Ready() const { return m_ready; }

private:
    struct Frame {
        const scene::Camera* camera = nullptr;
        world::World* world = nullptr;
        core::RenderConfig settings;
        float time = 0.0f;
        mat4 viewProj{1.0f};
        mat4 view{1.0f};
        mat4 lightSpace{1.0f};
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
    void RenderWaterPass();
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
    Shader m_bloomThresholdShader;
    Shader m_bloomBlurShader;
    Shader m_postShader;

    Texture m_grassTexture;
    Texture m_rockTexture;
    Texture m_soilTexture;

    Mesh m_fullscreenTriangle;
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
