// Frontier Engine - offscreen targets (HDR scene buffer, shadow map, bloom).
#pragma once

#include "math/Math.h"
#include "render/GL.h"

#include <string>

namespace frontier::render {

// Multisampled colour + depth target that resolves into a sampleable texture.
class SceneTarget {
public:
    SceneTarget() = default;
    ~SceneTarget();

    SceneTarget(const SceneTarget&) = delete;
    SceneTarget& operator=(const SceneTarget&) = delete;

    bool Create(int width, int height, int samples, bool hdr);
    void Destroy();
    bool Resize(int width, int height);

    // Binds the draw framebuffer and sets the viewport.
    void Bind() const;
    void Unbind() const;

    // Resolves MSAA into the sampleable texture (no-op for single-sample targets).
    void Resolve() const;

    GLuint ColorTexture() const { return m_colorTexture; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    int Samples() const { return m_samples; }
    bool Hdr() const { return m_hdr; }
    bool Valid() const { return m_drawFbo != 0; }
    bool IsMultisampled() const { return m_samples > 1; }

private:
    bool CreateAttachments();

    GLuint m_drawFbo = 0;
    GLuint m_resolveFbo = 0;
    GLuint m_colorTexture = 0;
    GLuint m_colorRenderbuffer = 0;
    GLuint m_depthRenderbuffer = 0;
    int m_width = 0;
    int m_height = 0;
    int m_samples = 1;
    bool m_hdr = true;
};

// Directional light shadow map with a fitted orthographic projection.
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    bool Create(int size);
    void Destroy();
    bool Resize(int size);

    void Bind() const;
    void Unbind() const;

    // Fits a light-space matrix around the camera view and snaps it to texel
    // increments to reduce shadow shimmering.
    mat4 ComputeLightMatrix(const vec3& sunDirection, const vec3& cameraPosition,
                            const vec3& cameraForward, float radius, float depth) const;

    GLuint Texture() const { return m_depthTexture; }
    int Size() const { return m_size; }
    bool Valid() const { return m_fbo != 0; }

private:
    GLuint m_fbo = 0;
    GLuint m_depthTexture = 0;
    int m_size = 0;
};

// Small separable blur chain used for the bloom contribution.
class BloomChain {
public:
    BloomChain() = default;
    ~BloomChain();

    BloomChain(const BloomChain&) = delete;
    BloomChain& operator=(const BloomChain&) = delete;

    bool Create(int width, int height);
    void Destroy();
    bool Resize(int width, int height);

    // Ping-pong targets: m_targets[0] holds the blurred result.
    GLuint TargetTexture(int index) const { return index == 0 ? m_colorTextureA : m_colorTextureB; }
    void BindTarget(int index) const;
    void Unbind() const;
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    bool Valid() const { return m_fbo != 0; }

private:
    GLuint m_fbo = 0;
    GLuint m_colorTextureA = 0;
    GLuint m_colorTextureB = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace frontier::render
