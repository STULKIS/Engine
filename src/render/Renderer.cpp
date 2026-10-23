#include "render/Renderer.h"

#include "core/Log.h"
#include "math/Math.h"
#include "render/GeometryBuilder.h"
#include "scene/Camera.h"
#include "scene/Vegetation.h"
#include "world/World.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace frontier::render {
namespace {

using Clock = std::chrono::high_resolution_clock;

float SecondsSince(const Clock::time_point& start) {
    return std::chrono::duration<float>(Clock::now() - start).count() * 1000.0f;
}

} // namespace

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(const core::RenderConfig& config, const std::string& assetDirectory) {
    m_config = config;
    m_assetDirectory = assetDirectory;

    if (!LoadPrograms(assetDirectory)) {
        FR_ERROR("Renderer initialization failed while loading shaders from '%s'.", assetDirectory.c_str());
        return false;
    }
    if (!CreateTextures()) {
        FR_ERROR("Renderer initialization failed while creating procedural textures.");
        return false;
    }

    if (!CreateTargets(m_windowWidth, m_windowHeight)) {
        FR_ERROR("Renderer initialization failed while creating render targets.");
        return false;
    }

    m_fullscreenTriangle.Create(geometry::MakeFullscreenTriangle());

    // Bind sampler units once per program; these are part of program state.
    m_terrainShader.Bind();
    m_terrainShader.SetInt("uGrassTexture", 0);
    m_terrainShader.SetInt("uRockTexture", 1);
    m_terrainShader.SetInt("uSoilTexture", 2);
    m_terrainShader.SetInt("uShadowMap", 3);

    m_waterShader.Bind();
    m_waterShader.SetInt("uHeightTexture", 4);
    m_waterShader.SetInt("uShadowMap", 3);

    m_foliageShader.Bind();
    m_foliageShader.SetInt("uShadowMap", 3);

    m_bloomThresholdShader.Bind();
    m_bloomThresholdShader.SetInt("uSceneTexture", 0);

    m_bloomBlurShader.Bind();
    m_bloomBlurShader.SetInt("uInputTexture", 0);

    m_postShader.Bind();
    m_postShader.SetInt("uSceneTexture", 0);
    m_postShader.SetInt("uBloomTexture", 1);

    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(0.02f, 0.03f, 0.05f, 1.0f);

    gl::CheckErrors("renderer-init");
    m_ready = true;
    FR_INFO("Renderer initialized: %dx%d output, %d MSAA, %s.",
            m_windowWidth, m_windowHeight, m_sceneTarget.Samples(), m_sceneTarget.Hdr() ? "HDR" : "LDR");
    return true;
}

bool Renderer::CreateTargets(int width, int height) {
    const int sceneWidth = std::max(1, static_cast<int>(static_cast<float>(width) * m_config.renderScale));
    const int sceneHeight = std::max(1, static_cast<int>(static_cast<float>(height) * m_config.renderScale));

    m_sceneRequestSamples = m_config.msaaSamples;
    m_sceneRequestHdr = m_config.hdr;
    if (!RecreateSceneTarget(sceneWidth, sceneHeight)) {
        return false;
    }

    if (!m_bloom.Create(sceneWidth, sceneHeight)) {
        FR_WARN("Bloom target creation failed; bloom will be disabled for this run.");
    }

    m_shadowRequestedSize = m_config.shadowMapSize;
    if (!m_shadowMap.Create(m_config.shadowMapSize)) {
        FR_WARN("Shadow map creation failed; shadows will be disabled for this run.");
    }
    return true;
}

bool Renderer::RecreateSceneTarget(int width, int height) {
    const int requestedSamples = std::max(1, m_config.msaaSamples);
    const bool requestedHdr = m_config.hdr;

    if (m_sceneTarget.Create(width, height, requestedSamples, requestedHdr)) {
        return true;
    }

    // Drivers differ in which multisampled / floating point combinations they
    // can render to. Degrade gracefully instead of ending up without a target.
    FR_WARN("No %dx MSAA %s target at %dx%d; retrying without MSAA.", requestedSamples,
            requestedHdr ? "HDR" : "LDR", width, height);
    if (requestedSamples > 1 && m_sceneTarget.Create(width, height, 1, requestedHdr)) {
        FR_WARN("Using a single-sample scene target (MSAA unavailable).");
        return true;
    }
    if (requestedHdr) {
        FR_WARN("Retrying with an 8-bit LDR scene target (bloom will be reduced).");
        if (m_sceneTarget.Create(width, height, 1, false)) {
            return true;
        }
    }
    FR_ERROR("Could not create any scene render target at %dx%d.", width, height);
    return false;
}

bool Renderer::LoadPrograms(const std::string& assetDirectory) {
    const std::string base = assetDirectory + "/shaders/";
    struct ProgramRequest {
        Shader* shader;
        const char* vertex;
        const char* fragment;
        const char* name;
    };
    const ProgramRequest requests[] = {
        {&m_skyShader, "sky.vert", "sky.frag", "sky"},
        {&m_terrainShader, "terrain.vert", "terrain.frag", "terrain"},
        {&m_foliageShader, "foliage.vert", "foliage.frag", "foliage"},
        {&m_waterShader, "water.vert", "water.frag", "water"},
        {&m_shadowShader, "shadow.vert", "shadow.frag", "shadow"},
        {&m_shadowInstancedShader, "shadow_instanced.vert", "shadow.frag", "shadow-instanced"},
        {&m_bloomThresholdShader, "fullscreen.vert", "bloom_threshold.frag", "bloom-threshold"},
        {&m_bloomBlurShader, "fullscreen.vert", "bloom_blur.frag", "bloom-blur"},
        {&m_postShader, "fullscreen.vert", "post.frag", "post"},
    };

    bool ok = true;
    for (const ProgramRequest& request : requests) {
        if (!request.shader->LoadFromFiles(base + request.vertex, base + request.fragment, request.name)) {
            ok = false;
        }
    }
    return ok;
}

bool Renderer::CreateTextures() {
    constexpr int kDetailSize = 512;
    const uint32_t seed = 20240607u;

    const std::vector<uint8_t> grass = procedural::GrassDetail(kDetailSize, seed);
    const std::vector<uint8_t> rock = procedural::RockDetail(kDetailSize, seed + 17u);
    const std::vector<uint8_t> soil = procedural::SoilDetail(kDetailSize, seed + 31u);

    TextureDesc desc;
    desc.generateMips = true;
    desc.repeat = true;
    // Colour detail maps are authored in sRGB.
    if (!m_grassTexture.CreateRGBA8(kDetailSize, kDetailSize, grass, true, desc) ||
        !m_rockTexture.CreateRGBA8(kDetailSize, kDetailSize, rock, true, desc) ||
        !m_soilTexture.CreateRGBA8(kDetailSize, kDetailSize, soil, true, desc)) {
        return false;
    }
    m_grassTexture.SetName("procedural-grass");
    m_rockTexture.SetName("procedural-rock");
    m_soilTexture.SetName("procedural-soil");
    FR_INFO("Generated procedural detail textures (%dx%d each).", kDetailSize, kDetailSize);
    return true;
}

void Renderer::Shutdown() {
    m_fullscreenTriangle.Destroy();
    m_bloom.Destroy();
    m_shadowMap.Destroy();
    m_sceneTarget.Destroy();
    m_grassTexture.Destroy();
    m_rockTexture.Destroy();
    m_soilTexture.Destroy();
    m_skyShader.Destroy();
    m_terrainShader.Destroy();
    m_foliageShader.Destroy();
    m_waterShader.Destroy();
    m_shadowShader.Destroy();
    m_shadowInstancedShader.Destroy();
    m_bloomThresholdShader.Destroy();
    m_bloomBlurShader.Destroy();
    m_postShader.Destroy();
    m_ready = false;
}

void Renderer::Resize(int width, int height) {
    m_windowWidth = std::max(1, width);
    m_windowHeight = std::max(1, height);
    UpdateTargets();
}

void Renderer::UpdateTargets() {
    const int sceneWidth = std::max(1, static_cast<int>(static_cast<float>(m_windowWidth) * m_config.renderScale));
    const int sceneHeight = std::max(1, static_cast<int>(static_cast<float>(m_windowHeight) * m_config.renderScale));

    // Compare against the requested settings, not the effective ones: a driver
    // may clamp the MSAA count, and comparing to the clamp would recreate the
    // targets on every single frame.
    const bool settingsChanged = m_sceneRequestSamples != m_config.msaaSamples ||
                                 m_sceneRequestHdr != m_config.hdr;
    const bool sizeChanged = !m_sceneTarget.Valid() || m_sceneTarget.Width() != sceneWidth ||
                             m_sceneTarget.Height() != sceneHeight;
    if (settingsChanged || sizeChanged) {
        m_sceneRequestSamples = m_config.msaaSamples;
        m_sceneRequestHdr = m_config.hdr;
        if (sizeChanged && !settingsChanged && m_sceneTarget.Valid()) {
            if (!m_sceneTarget.Resize(sceneWidth, sceneHeight)) {
                FR_WARN("Scene target resize to %dx%d failed; recreating.", sceneWidth, sceneHeight);
                RecreateSceneTarget(sceneWidth, sceneHeight);
            }
            m_bloom.Resize(sceneWidth, sceneHeight);
        } else {
            RecreateSceneTarget(sceneWidth, sceneHeight);
            m_bloom.Create(sceneWidth, sceneHeight);
        }
    }

    // Only retry the shadow map when the requested size actually changes so a
    // driver that cannot allocate one does not stall the frame in a retry loop.
    if (m_shadowRequestedSize != m_config.shadowMapSize) {
        m_shadowRequestedSize = m_config.shadowMapSize;
        if (!m_shadowMap.Create(m_config.shadowMapSize)) {
            FR_WARN("Shadow map creation failed at %d; shadows disabled.", m_config.shadowMapSize);
        }
    }
}

void Renderer::BindTexture(int unit, const Texture& texture) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture.Id());
}

void Renderer::Render(const scene::Camera& camera, world::World& world, const core::RenderConfig& config,
                      float simulationTime) {
    if (!m_ready) {
        return;
    }
    m_config = config;
    m_stats = {};
    m_frame.camera = &camera;
    m_frame.world = &world;
    m_frame.settings = config;
    m_frame.time = simulationTime;
    m_frame.view = camera.ViewMatrix();
    m_frame.viewProj = camera.ViewProjection();
    m_frame.cameraPos = camera.Position();
    m_frame.aspect = camera.Aspect();

    UpdateTargets();
    if (!m_sceneTarget.Valid()) {
        FR_ERROR("Scene target is not available; skipping frame rendering.");
        return;
    }
    world.Vegetation().UploadVisible();
    m_stats.sceneWidth = m_sceneTarget.Width();
    m_stats.sceneHeight = m_sceneTarget.Height();
    m_stats.msaaSamples = m_sceneTarget.Samples();
    m_stats.shadowMapSize = m_shadowMap.Size();

    const world::LightingState& lighting = world.Lighting();
    const float shadowRadius = std::min(config.viewDistance * 0.35f, 170.0f);
    const bool castShadows = config.showShadows && m_shadowMap.Valid() &&
                             lighting.shadowStrength > 0.01f;
    m_frame.lightSpace = castShadows
                             ? m_shadowMap.ComputeLightMatrix(lighting.lightDirection, camera.Position(),
                                                              camera.Forward(), shadowRadius, 420.0f)
                             : mat4(1.0f);

    const auto frameStart = Clock::now();
    if (castShadows) {
        RenderShadowPass();
    }
    m_stats.shadowPassMs = SecondsSince(frameStart);

    const auto mainStart = Clock::now();
    m_sceneTarget.Bind();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (config.showSky) {
        RenderSkyPass();
    }
    RenderTerrainPass();
    if (config.showFoliage) {
        RenderFoliagePass();
    }
    if (config.showWater) {
        RenderWaterPass();
    }

    m_sceneTarget.Resolve();
    m_stats.mainPassMs = SecondsSince(mainStart);

    const auto postStart = Clock::now();
    RenderBloomPass();
    RenderPostPass();
    m_stats.postPassMs = SecondsSince(postStart);

    const scene::VegetationStats& vegetationStats = world.Vegetation().Stats();
    m_stats.visibleInstances = vegetationStats.visibleInstances;
    m_stats.culledInstances = vegetationStats.culledInstances;
}

void Renderer::SetAtmosphereUniforms(Shader& shader) {
    const world::LightingState& lighting = m_frame.world->Lighting();
    shader.SetVec3("uCameraPos", m_frame.cameraPos);
    shader.SetVec3("uSunDirection", lighting.lightDirection);
    shader.SetVec3("uSunColor", lighting.lightColor);
    shader.SetFloat("uSunIntensity", lighting.lightIntensity);
    shader.SetVec3("uAmbientSky", lighting.ambientSky);
    shader.SetVec3("uAmbientGround", lighting.ambientGround);
    shader.SetVec3("uFogColor", lighting.fogColor);
    shader.SetFloat("uFogDensity", m_frame.settings.fogDensity);
    shader.SetFloat("uFogHeightFalloff", m_frame.settings.fogHeightFalloff);
    shader.SetFloat("uViewDistance", m_frame.settings.viewDistance);
    shader.SetFloat("uTime", m_frame.time);

    // Stylised shading controls (ignored by the realistic path).
    const bool cel = m_frame.settings.style == core::RenderStyle::Anime;
    shader.SetInt("uCelShading", cel ? 1 : 0);
    shader.SetFloat("uCelSteps", static_cast<float>(m_frame.settings.celSteps));
    shader.SetFloat("uRimStrength", cel ? m_frame.settings.rimStrength : 0.0f);
    shader.SetVec3("uRimColor", vec3(0.62f, 0.78f, 1.0f));
}

void Renderer::SetShadowUniforms(Shader& shader) {
    const bool enabled = m_frame.settings.showShadows && m_shadowMap.Valid() &&
                         m_frame.world->Lighting().shadowStrength > 0.01f;
    shader.SetInt("uShadowEnabled", enabled ? 1 : 0);
    shader.SetMat4("uLightSpaceMatrix", m_frame.lightSpace);
    shader.SetFloat("uShadowTexelSize", m_shadowMap.Size() > 0 ? 1.0f / static_cast<float>(m_shadowMap.Size()) : 0.0f);
    shader.SetInt("uShadowMap", 3);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture());
}

void Renderer::RenderShadowPass() {
    // Direct shadow map binding (the shadow map owns a raw texture id).
    m_shadowMap.Bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Render both sides of open geometry (terrain, grass cards) so mountains and
    // tree canopies cast onto the scene. Bias is handled in the fragment shaders.
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    m_shadowShader.Bind();
    m_shadowShader.SetMat4("uLightSpaceMatrix", m_frame.lightSpace);

    // Terrain.
    glActiveTexture(GL_TEXTURE0);
    m_shadowShader.SetMat4("uModel", mat4(1.0f));
    world::World* world = m_frame.world;
    m_stats.shadowDrawCalls = 0;
    m_stats.shadowTriangles = 0;
    if (world->Terrain().Mesh().Valid()) {
        world->Terrain().Mesh().Draw();
        ++m_stats.shadowDrawCalls;
        m_stats.shadowTriangles += world->Terrain().Mesh().TriangleCount();
    }

    // Instanced casters.
    m_shadowInstancedShader.Bind();
    m_shadowInstancedShader.SetMat4("uLightSpaceMatrix", m_frame.lightSpace);
    m_shadowInstancedShader.SetFloat("uTime", m_frame.time);
    m_shadowInstancedShader.SetVec3("uWindDirection", world->WindDirection());
    m_shadowInstancedShader.SetFloat("uWindStrength", world->WindStrength());

    if (m_frame.settings.shadowsFromFoliage && m_frame.settings.showFoliage) {
        for (const scene::FoliageSet& set : world->Vegetation().Sets()) {
            if (!set.castsShadow) {
                continue;
            }
            for (size_t lod = 0; lod < set.meshes.size(); ++lod) {
                // Far billboards are not worth shadow casting.
                if (lod >= 2 || lod >= set.visibleCounts.size()) {
                    continue;
                }
                const int count = set.visibleCounts[lod];
                if (count <= 0) {
                    continue;
                }
                set.meshes[lod].DrawInstanced(count);
                ++m_stats.shadowDrawCalls;
                m_stats.shadowTriangles += set.meshes[lod].TriangleCount() * static_cast<size_t>(count);
            }
        }
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    m_shadowMap.Unbind();
}

void Renderer::RenderSkyPass() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    const world::LightingState& lighting = m_frame.world->Lighting();
    const scene::SunSample& sun = lighting.sun;

    m_skyShader.Bind();
    m_skyShader.SetMat4("uInvViewProj", glm::inverse(m_frame.viewProj));
    m_skyShader.SetVec3("uCameraPos", m_frame.cameraPos);
    m_skyShader.SetFloat("uTime", m_frame.time);
    m_skyShader.SetVec3("uSunPosition", sun.position);
    m_skyShader.SetVec3("uMoonPosition", lighting.moonPosition);
    m_skyShader.SetVec3("uSunColor", sun.color);
    m_skyShader.SetFloat("uSunIntensity", sun.intensity);
    m_skyShader.SetFloat("uDaylight", lighting.daylight);
    m_skyShader.SetFloat("uStars", lighting.stars);
    m_skyShader.SetInt("uAnimeSky", m_config.style == core::RenderStyle::Anime ? 1 : 0);
    m_fullscreenTriangle.Draw();
    ++m_stats.drawCalls;
    m_stats.triangles += 1;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void Renderer::RenderTerrainPass() {
    const scene::Terrain& terrain = m_frame.world->Terrain();
    if (!terrain.Mesh().Valid()) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, m_frame.settings.wireframe ? GL_LINE : GL_FILL);

    m_terrainShader.Bind();
    m_terrainShader.SetMat4("uViewProj", m_frame.viewProj);
    SetAtmosphereUniforms(m_terrainShader);
    SetShadowUniforms(m_terrainShader);
    m_terrainShader.SetFloat("uWaterLevel", terrain.WaterLevel());
    m_terrainShader.SetFloat("uSnowLine", terrain.MaxHeight() - (terrain.MaxHeight() - terrain.MinHeight()) * 0.12f);

    BindTexture(0, m_grassTexture);
    BindTexture(1, m_rockTexture);
    BindTexture(2, m_soilTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture());

    terrain.Mesh().Draw();
    ++m_stats.drawCalls;
    m_stats.triangles += terrain.Mesh().TriangleCount();
}

void Renderer::RenderFoliagePass() {
    world::World* world = m_frame.world;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); // grass blades and cross cards are double sided
    glPolygonMode(GL_FRONT_AND_BACK, m_frame.settings.wireframe ? GL_LINE : GL_FILL);

    m_foliageShader.Bind();
    m_foliageShader.SetMat4("uViewProj", m_frame.viewProj);
    SetAtmosphereUniforms(m_foliageShader);
    SetShadowUniforms(m_foliageShader);
    m_foliageShader.SetVec3("uWindDirection", world->WindDirection());
    m_foliageShader.SetFloat("uWindStrength", world->WindStrength());
    m_foliageShader.SetInt("uWindEnabled", 1);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture());

    for (const scene::FoliageSet& set : world->Vegetation().Sets()) {
        for (size_t lod = 0; lod < set.meshes.size(); ++lod) {
            if (lod >= set.visibleCounts.size()) {
                continue;
            }
            const int count = set.visibleCounts[lod];
            if (count <= 0) {
                continue;
            }
            set.meshes[lod].DrawInstanced(count);
            ++m_stats.drawCalls;
            m_stats.triangles += set.meshes[lod].TriangleCount() * static_cast<size_t>(count);
        }
    }

    glEnable(GL_CULL_FACE);
}

void Renderer::RenderWaterPass() {
    world::World* world = m_frame.world;
    const scene::Water& water = world->Water();
    if (!water.Valid() || !world->Terrain().HeightTexture().Valid()) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, m_frame.settings.wireframe ? GL_LINE : GL_FILL);

    m_waterShader.Bind();
    m_waterShader.SetMat4("uViewProj", m_frame.viewProj);
    SetAtmosphereUniforms(m_waterShader);
    SetShadowUniforms(m_waterShader);
    m_waterShader.SetVec3("uSunPosition", world->Lighting().sun.position);
    m_waterShader.SetFloat("uWaterLevel", water.Level());
    m_waterShader.SetFloat("uTerrainSize", world->Terrain().Size());
    BindTexture(4, world->Terrain().HeightTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture());

    water.Mesh().Draw();
    ++m_stats.drawCalls;
    m_stats.triangles += water.Mesh().TriangleCount();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Renderer::RenderBloomPass() {
    if (!m_config.bloom || !m_bloom.Valid() || !m_sceneTarget.Valid()) {
        return;
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    const float texelX = 1.0f / static_cast<float>(m_bloom.Width());
    const float texelY = 1.0f / static_cast<float>(m_bloom.Height());

    // Bright pass: scene (HDR) -> bloom target A.
    m_bloom.BindTarget(0);
    m_bloomThresholdShader.Bind();
    m_bloomThresholdShader.SetFloat("uThreshold", 1.05f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTarget.ColorTexture());
    m_fullscreenTriangle.Draw();

    // Two separable blur iterations with increasing radius.
    m_bloomBlurShader.Bind();
    m_bloomBlurShader.SetInt("uInputTexture", 0);
    for (int iteration = 0; iteration < 2; ++iteration) {
        const float radius = 1.0f + static_cast<float>(iteration) * 1.75f;

        m_bloom.BindTarget(1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloom.TargetTexture(0));
        m_bloomBlurShader.SetVec2("uDirection", vec2(texelX * radius, 0.0f));
        m_fullscreenTriangle.Draw();

        m_bloom.BindTarget(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloom.TargetTexture(1));
        m_bloomBlurShader.SetVec2("uDirection", vec2(0.0f, texelY * radius));
        m_fullscreenTriangle.Draw();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderPostPass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    m_postShader.Bind();
    const bool useBloom = m_config.bloom && m_bloom.Valid();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTarget.ColorTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, useBloom ? m_bloom.TargetTexture(0) : m_sceneTarget.ColorTexture());
    m_postShader.SetFloat("uExposure", m_config.exposure);
    m_postShader.SetFloat("uBloomStrength", useBloom ? m_config.bloomStrength : 0.0f);
    m_postShader.SetInt("uUseBloom", useBloom ? 1 : 0);
    m_postShader.SetInt("uUseTonemap", 1);

    const bool cel = m_config.style == core::RenderStyle::Anime;
    const bool outline = cel && m_config.outlines && m_sceneTarget.DepthTexture() != 0;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_sceneTarget.DepthTexture());
    m_postShader.SetInt("uDepthTexture", 2);
    m_postShader.SetVec2("uInvResolution",
                         vec2(1.0f / static_cast<float>(std::max(1, m_sceneTarget.Width())),
                              1.0f / static_cast<float>(std::max(1, m_sceneTarget.Height()))));
    m_postShader.SetInt("uOutlineEnabled", outline ? 1 : 0);
    m_postShader.SetFloat("uOutlineThickness", m_config.outlineThickness);
    m_postShader.SetFloat("uOutlineStrength", m_config.outlineStrength);
    m_postShader.SetVec3("uOutlineColor", vec3(0.05f, 0.045f, 0.08f));
    m_postShader.SetFloat("uNearPlane", m_frame.camera ? m_frame.camera->NearPlane() : 0.1f);
    m_postShader.SetFloat("uFarPlane", m_frame.camera ? m_frame.camera->FarPlane() : 1000.0f);
    m_postShader.SetInt("uCelShading", cel ? 1 : 0);
    m_postShader.SetFloat("uSaturation", cel ? m_config.saturation * 1.08f : m_config.saturation);
    m_postShader.SetInt("uPosterizeLevels", m_config.posterizeLevels);
    m_postShader.SetFloat("uVignette", cel ? 0.12f : 0.28f);
    m_fullscreenTriangle.Draw();
    ++m_stats.drawCalls;

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

bool Renderer::SaveScreenshot(const std::string& path) const {
    if (!m_ready) {
        return false;
    }
    const int width = m_windowWidth;
    const int height = m_windowHeight;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        FR_ERROR("Could not open screenshot path '%s'.", path.c_str());
        return false;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    // OpenGL rows are bottom-up; flip while writing.
    for (int y = height - 1; y >= 0; --y) {
        std::fwrite(pixels.data() + static_cast<size_t>(y) * width * 3, 1,
                    static_cast<size_t>(width) * 3, file);
    }
    std::fclose(file);
    FR_INFO("Screenshot saved to %s", path.c_str());
    return true;
}

} // namespace frontier::render
