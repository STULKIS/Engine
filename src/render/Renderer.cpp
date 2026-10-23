#include "render/Renderer.h"

#include "asset/Placeholders.h"
#include "core/Log.h"
#include "math/Math.h"
#include "render/GeometryBuilder.h"
#include "scene/Camera.h"
#include "scene/Scene.h"
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
    if (!CreatePrimitiveMeshes()) {
        FR_ERROR("Renderer initialization failed while creating the primitive meshes.");
        return false;
    }
    if (!LoadAssets(assetDirectory)) {
        FR_ERROR("Renderer initialization failed while loading assets from '%s'.",
                 assetDirectory.c_str());
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

    m_objectShader.Bind();
    m_objectShader.SetInt("uShadowMap", 3);

    m_assetShader.Bind();
    m_assetShader.SetInt("uShadowMap", 3);
    m_assetShader.SetInt("uAlbedoTexture", 5);

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
        {&m_objectShader, "object.vert", "object.frag", "object"},
        {&m_assetShader, "asset.vert", "asset.frag", "asset"},
        {&m_shadowAssetShader, "shadow_asset.vert", "shadow.frag", "shadow-asset"},
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

bool Renderer::CreatePrimitiveMeshes() {
    const int primitiveCount = static_cast<int>(scene::Primitive::Count);
    m_primitiveInstances.assign(static_cast<size_t>(primitiveCount), {});
    m_primitiveDrawCounts.assign(static_cast<size_t>(primitiveCount), 0);

    bool ok = true;
    // Unit-sized shapes: the entity transform supplies position and scale.
    ok = ok && m_primitiveMeshes[static_cast<int>(scene::Primitive::Box)].Create(
                   geometry::MakeBox(vec3(0.5f), vec4(1.0f)));
    ok = ok && m_primitiveMeshes[static_cast<int>(scene::Primitive::Sphere)].Create(
                   geometry::MakeSphere(0.5f, 16, 24));
    ok = ok && m_primitiveMeshes[static_cast<int>(scene::Primitive::Cylinder)].Create(
                   geometry::MakeCylinder(vec3(0.0f, -0.5f, 0.0f), vec3(0.0f, 0.5f, 0.0f),
                                          0.5f, 0.5f, 20, vec4(1.0f)));
    ok = ok && m_primitiveMeshes[static_cast<int>(scene::Primitive::Cone)].Create(
                   geometry::MakeCylinder(vec3(0.0f, -0.5f, 0.0f), vec3(0.0f, 0.5f, 0.0f),
                                          0.5f, 0.0f, 20, vec4(1.0f)));
    if (!ok) {
        FR_ERROR("Could not create the built-in primitive meshes.");
        return false;
    }
    FR_INFO("Scene primitives ready: box, sphere, cylinder, cone.");
    return true;
}

void Renderer::BuildInstanceBuffers() {
    for (auto& list : m_primitiveInstances) {
        list.clear();
    }
    if (!m_frame.entities) {
        return;
    }
    for (const scene::Entity& entity : m_frame.entities->Entities()) {
        if (!entity.alive || !entity.visible) {
            continue;
        }
        // Entities backed by an imported model are drawn by the asset pass.
        if (!entity.mesh.empty() && m_assetMeshes.find(entity.mesh) != m_assetMeshes.end()) {
            continue;
        }
        const int index = static_cast<int>(entity.primitive);
        if (index < 0 || index >= static_cast<int>(m_primitiveInstances.size())) {
            continue;
        }
        InstanceData instance;
        instance.transform = m_frame.entities->WorldMatrix(entity);
        // Alpha is the wind phase: props do not sway, so keep it at 0.
        instance.tint = vec4(entity.color, 0.0f);
        m_primitiveInstances[static_cast<size_t>(index)].push_back(instance);
    }
    for (size_t i = 0; i < m_primitiveInstances.size(); ++i) {
        auto& list = m_primitiveInstances[i];
        m_primitiveDrawCounts[i] = static_cast<int>(list.size());
        if (!list.empty()) {
            m_primitiveMeshes[i].UploadInstances(list);
        }
    }
}

bool Renderer::LoadAssets(const std::string& assetDirectory) {
    std::string error;
    // Placeholder art is written once so a fresh checkout has something to show.
    if (!asset::EnsurePlaceholderAssets(assetDirectory, error)) {
        FR_WARN("Could not write placeholder assets: %s", error.c_str());
    }
    if (!m_assets.Scan(assetDirectory, error) && !error.empty()) {
        FR_WARN("Asset scan: %s", error.c_str());
    }

    int uploadedMeshes = 0;
    for (const asset::MeshAsset& source : m_assets.Meshes()) {
        render::MeshData data;
        data.vertices.reserve(source.mesh.vertices.size());
        for (const asset::Vertex& vertex : source.mesh.vertices) {
            render::Vertex converted;
            converted.position = vertex.position;
            converted.normal = vertex.normal;
            converted.uv = vertex.uv;
            converted.color = vec4(1.0f);
            data.vertices.push_back(converted);
        }
        data.indices = source.mesh.indices;
        data.RecomputeBounds();

        Mesh gpu;
        if (!gpu.Create(data)) {
            FR_WARN("Could not upload mesh '%s'.", source.name.c_str());
            continue;
        }
        m_assetMeshes.emplace(source.name, std::move(gpu));
        ++uploadedMeshes;
    }

    int uploadedTextures = 0;
    for (const asset::TextureAsset& source : m_assets.Textures()) {
        TextureDesc desc;
        desc.generateMips = true;
        desc.repeat = true;
        Texture gpu;
        if (!gpu.CreateRGBA8(source.image.width, source.image.height, source.image.pixels, true, desc)) {
            FR_WARN("Could not upload texture '%s'.", source.name.c_str());
            continue;
        }
        gpu.SetName(source.name);
        m_assetTextures.emplace(source.name, std::move(gpu));
        ++uploadedTextures;
    }

    FR_INFO("Asset pipeline: %d/%zu meshes and %d/%zu textures uploaded.", uploadedMeshes,
            m_assets.Meshes().size(), uploadedTextures, m_assets.Textures().size());
    return true;
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
    for (Mesh& mesh : m_primitiveMeshes) {
        mesh.Destroy();
    }
    for (auto& entry : m_assetMeshes) {
        entry.second.Destroy();
    }
    m_assetMeshes.clear();
    for (auto& entry : m_assetTextures) {
        entry.second.Destroy();
    }
    m_assetTextures.clear();
    m_assets.Clear();

    m_skyShader.Destroy();
    m_objectShader.Destroy();
    m_assetShader.Destroy();
    m_shadowAssetShader.Destroy();
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

void Renderer::Render(const scene::Camera& camera, world::World& world, scene::Scene& entities,
                      const core::RenderConfig& config, float simulationTime) {
    if (!m_ready) {
        return;
    }
    m_config = config;
    m_stats = {};
    m_frame.camera = &camera;
    m_frame.world = &world;
    m_frame.entities = &entities;
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
    BuildInstanceBuffers();
    m_stats.sceneWidth = m_sceneTarget.Width();
    m_stats.sceneHeight = m_sceneTarget.Height();
    m_stats.msaaSamples = m_sceneTarget.Samples();
    m_stats.shadowMapSize = m_shadowMap.Size();

    const world::LightingState& lighting = world.Lighting();
    const bool castShadows = config.showShadows && m_shadowMap.Valid() &&
                             lighting.shadowStrength > 0.01f;

    // Two cascades: the near one is fitted tightly around the camera so each
    // leaf and rock gets a handful of shadow texels, the far one covers the
    // landscape. Splitting is what removes the blocky shadow edges that a single
    // 200 m-wide cascade produces at 2048 textures.
    const float nearRadius = std::min(std::max(config.shadowNearRadius, 12.0f), 90.0f);
    const float farRadius = std::min(config.viewDistance * 0.42f, 420.0f);
    m_frame.cascadeSplit = nearRadius * 1.15f;
    if (castShadows) {
        m_frame.lightSpace = m_shadowMap.ComputeLightMatrix(0, lighting.lightDirection, camera.Position(),
                                                            camera.Forward(), nearRadius, 300.0f);
        m_frame.lightSpaceFar = m_shadowMap.ComputeLightMatrix(1, lighting.lightDirection, camera.Position(),
                                                               camera.Forward(), farRadius, 700.0f);
    } else {
        m_frame.lightSpace = mat4(1.0f);
        m_frame.lightSpaceFar = mat4(1.0f);
    }

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
    RenderObjectPass();
    RenderAssetPass();
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
    // Cool, slightly violet shadow tint keeps material hue readable in shade.
    shader.SetVec3("uShadowTint", vec3(0.46f, 0.50f, 0.72f));
    shader.SetFloat("uSpecularStrength", cel ? m_frame.settings.specularStrength : 0.0f);
    shader.SetFloat("uWarmEdge", cel ? m_frame.settings.warmEdge : 0.0f);
}

void Renderer::SetShadowUniforms(Shader& shader) {
    const bool enabled = m_frame.settings.showShadows && m_shadowMap.Valid() &&
                         m_frame.world->Lighting().shadowStrength > 0.01f;
    shader.SetInt("uShadowEnabled", enabled ? 1 : 0);
    shader.SetMat4("uLightSpaceMatrix", m_frame.lightSpace);
    shader.SetMat4("uLightSpaceMatrixFar", m_frame.lightSpaceFar);
    const float texel = m_shadowMap.Size() > 0 ? 1.0f / static_cast<float>(m_shadowMap.Size()) : 0.0f;
    shader.SetFloat("uShadowTexelSize", texel);
    shader.SetFloat("uShadowTexelSizeFar", texel);
    shader.SetFloat("uCascadeSplit", m_frame.cascadeSplit);
    shader.SetInt("uShadowMap", 3);
    shader.SetInt("uShadowMapFar", 5);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture(0));
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture(1));
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::RenderShadowPass() {
    world::World* world = m_frame.world;
    m_stats.shadowDrawCalls = 0;
    m_stats.shadowTriangles = 0;

    for (int cascade = 0; cascade < ShadowMap::kCascades; ++cascade) {
        const mat4& lightSpace = cascade == 0 ? m_frame.lightSpace : m_frame.lightSpaceFar;

        m_shadowMap.Bind(cascade);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        // Render both sides of open geometry (terrain, grass cards) so mountains
        // and tree canopies cast onto the scene. Bias is handled in the shaders.
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_shadowShader.Bind();
        m_shadowShader.SetMat4("uLightSpaceMatrix", lightSpace);

        glActiveTexture(GL_TEXTURE0);
        m_shadowShader.SetMat4("uModel", mat4(1.0f));
        if (world->Terrain().Mesh().Valid()) {
            world->Terrain().Mesh().Draw();
            ++m_stats.shadowDrawCalls;
            m_stats.shadowTriangles += world->Terrain().Mesh().TriangleCount();
        }

        // Instanced casters. The far cascade uses the cheap LODs only.
        m_shadowInstancedShader.Bind();
        m_shadowInstancedShader.SetMat4("uLightSpaceMatrix", lightSpace);
        m_shadowInstancedShader.SetFloat("uTime", m_frame.time);
        m_shadowInstancedShader.SetVec3("uWindDirection", world->WindDirection());
        m_shadowInstancedShader.SetFloat("uWindStrength", world->WindStrength());

        if (m_frame.settings.shadowsFromFoliage && m_frame.settings.showFoliage) {
            for (const scene::FoliageSet& set : world->Vegetation().Sets()) {
                if (!set.castsShadow) {
                    continue;
                }
                for (size_t lod = 0; lod < set.meshes.size(); ++lod) {
                    // Far billboards never cast; the far cascade skips the
                    // densest tier to keep its cost bounded.
                    const size_t maxLod = cascade == 0 ? 2u : 2u;
                    if (lod >= maxLod || lod >= set.visibleCounts.size()) {
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

        // Scene entities cast shadows too (skip the far cascade for tiny props).
        if (m_frame.entities && !m_primitiveDrawCounts.empty()) {
            m_shadowInstancedShader.SetMat4("uLightSpaceMatrix", lightSpace);
            m_shadowInstancedShader.SetFloat("uTime", 0.0f);
            m_shadowInstancedShader.SetFloat("uWindStrength", 0.0f);
            for (size_t i = 0; i < m_primitiveDrawCounts.size(); ++i) {
                const int count = m_primitiveDrawCounts[i];
                if (count <= 0 || !m_primitiveMeshes[i].Valid()) {
                    continue;
                }
                m_primitiveMeshes[i].DrawInstanced(count);
                ++m_stats.shadowDrawCalls;
                m_stats.shadowTriangles += m_primitiveMeshes[i].TriangleCount() * static_cast<size_t>(count);
            }
        }

        // Imported meshes: one draw per caster, depth only.
        if (m_frame.entities && !m_assetMeshes.empty()) {
            bool bound = false;
            for (const scene::Entity& entity : m_frame.entities->Entities()) {
                if (!entity.alive || !entity.visible || !entity.castShadow || entity.mesh.empty()) {
                    continue;
                }
                auto mesh = m_assetMeshes.find(entity.mesh);
                if (mesh == m_assetMeshes.end()) {
                    continue;
                }
                if (!bound) {
                    m_shadowAssetShader.Bind();
                    m_shadowAssetShader.SetMat4("uLightSpaceMatrix", lightSpace);
                    bound = true;
                }
                m_shadowAssetShader.SetMat4("uModel", m_frame.entities->WorldMatrix(entity));
                mesh->second.Draw();
                ++m_stats.shadowDrawCalls;
                m_stats.shadowTriangles += mesh->second.TriangleCount();
            }
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
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
    m_foliageShader.SetFloat("uTranslucency", m_frame.settings.leafTranslucency);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.Texture());

    for (const scene::FoliageSet& set : world->Vegetation().Sets()) {
        m_foliageShader.SetFloat("uOutlineMask", set.inksOutline ? 0.0f : 1.0f);
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

void Renderer::RenderObjectPass() {
    if (!m_frame.entities || m_primitiveDrawCounts.empty()) {
        return;
    }
    bool any = false;
    for (const int count : m_primitiveDrawCounts) {
        any = any || count > 0;
    }
    if (!any) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glPolygonMode(GL_FRONT_AND_BACK, m_frame.settings.wireframe ? GL_LINE : GL_FILL);

    m_objectShader.Bind();
    m_objectShader.SetMat4("uViewProj", m_frame.viewProj);
    SetAtmosphereUniforms(m_objectShader);
    SetShadowUniforms(m_objectShader);

    for (size_t i = 0; i < m_primitiveDrawCounts.size(); ++i) {
        const int count = m_primitiveDrawCounts[i];
        if (count <= 0 || !m_primitiveMeshes[i].Valid()) {
            continue;
        }
        m_primitiveMeshes[i].DrawInstanced(count);
        ++m_stats.drawCalls;
        m_stats.triangles += m_primitiveMeshes[i].TriangleCount() * static_cast<size_t>(count);
    }
}

void Renderer::RenderAssetPass() {
    if (!m_frame.entities || m_assetMeshes.empty()) {
        return;
    }

    bool bound = false;
    for (const scene::Entity& entity : m_frame.entities->Entities()) {
        if (!entity.alive || !entity.visible || entity.mesh.empty()) {
            continue;
        }
        auto mesh = m_assetMeshes.find(entity.mesh);
        if (mesh == m_assetMeshes.end()) {
            continue;
        }
        if (!bound) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glPolygonMode(GL_FRONT_AND_BACK, m_frame.settings.wireframe ? GL_LINE : GL_FILL);

            m_assetShader.Bind();
            m_assetShader.SetMat4("uViewProj", m_frame.viewProj);
            SetAtmosphereUniforms(m_assetShader);
            SetShadowUniforms(m_assetShader);
            bound = true;
        }

        const mat4 model = m_frame.entities->WorldMatrix(entity);
        m_assetShader.SetMat4("uModel", model);
        m_assetShader.SetMat3("uNormalMatrix", mat3(glm::transpose(glm::inverse(model))));
        m_assetShader.SetVec3("uColor", entity.color);

        const auto texture = entity.texture.empty() ? m_assetTextures.end()
                                                    : m_assetTextures.find(entity.texture);
        if (texture != m_assetTextures.end()) {
            texture->second.Bind(5);
            m_assetShader.SetInt("uHasTexture", 1);
        } else {
            m_assetShader.SetInt("uHasTexture", 0);
        }

        mesh->second.Draw();
        ++m_stats.drawCalls;
        m_stats.triangles += mesh->second.TriangleCount();
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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

    // Sun position in screen space, for the light shafts. The sun is projected
    // from far away along its direction, so it stays put as the camera walks.
    const world::LightingState& lighting = m_frame.world->Lighting();
    const vec3 sunWorld = m_frame.cameraPos + lighting.sun.position * 3000.0f;
    const vec4 sunClip = m_frame.viewProj * vec4(sunWorld, 1.0f);
    vec2 sunScreen(0.5f, 0.5f);
    float sunVisibility = 0.0f;
    if (sunClip.w > 0.0f) {
        const vec3 ndc = vec3(sunClip) / sunClip.w;
        sunScreen = vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
        const bool onScreen = sunScreen.x > -0.15f && sunScreen.x < 1.15f &&
                              sunScreen.y > -0.15f && sunScreen.y < 1.15f;
        sunVisibility = onScreen ? std::clamp(lighting.sun.elevationDegrees / 12.0f, 0.0f, 1.0f) : 0.0f;
    }

    m_postShader.SetInt("uGodRaysEnabled", (cel && m_config.godRays) ? 1 : 0);
    m_postShader.SetFloat("uGodRayStrength", m_config.godRayStrength);
    m_postShader.SetVec2("uSunScreenPos", sunScreen);
    m_postShader.SetFloat("uSunVisibility", sunVisibility);
    m_postShader.SetVec3("uSunColorPost", lighting.sun.color);
    m_postShader.SetFloat("uContrast", m_config.contrast);
    m_postShader.SetFloat("uTemperature", m_config.temperature);
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
