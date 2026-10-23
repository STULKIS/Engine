#include "render/Framebuffer.h"

#include "core/Log.h"

#include <algorithm>

namespace frontier::render {
namespace {

GLenum CheckFramebuffer(const char* name) {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FR_ERROR("Framebuffer '%s' incomplete (0x%04X)", name, static_cast<unsigned>(status));
    }
    return status;
}

GLuint CreateColorTexture(int width, int height, bool hdr) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint internalFormat = hdr ? GL_RGBA16F : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA,
                 hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint CreateDepthTexture(int width, int height) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    // Sampled by hand for the outline shader, so no compare mode and no
    // filtering (LINEAR on a depth texture is only valid with compare mode).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace

SceneTarget::~SceneTarget() {
    Destroy();
}

bool SceneTarget::Create(int width, int height, int samples, bool hdr) {
    Destroy();
    m_width = width;
    m_height = height;
    m_hdr = hdr;

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples = std::clamp(samples, 1, std::max(1, maxSamples));
    if (m_samples != samples && samples > 1) {
        FR_WARN("Requested %dx MSAA but the driver caps at %d; using %d.", samples, maxSamples, m_samples);
    }
    if (m_samples > 4) {
        // Higher counts are rarely worth the bandwidth for this scene.
        m_samples = std::min(m_samples, 8);
    }

    return CreateAttachments();
}

bool SceneTarget::CreateAttachments() {
    m_colorTexture = CreateColorTexture(m_width, m_height, m_hdr);
    m_depthTexture = CreateDepthTexture(m_width, m_height);

    glGenFramebuffers(1, &m_drawFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);

    if (m_samples > 1) {
        glGenRenderbuffers(1, &m_colorRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_colorRenderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples,
                                         m_hdr ? GL_RGBA16F : GL_RGBA8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRenderbuffer);

        glGenRenderbuffers(1, &m_depthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRenderbuffer);

        if (CheckFramebuffer("scene-multisample") != GL_FRAMEBUFFER_COMPLETE) {
            Destroy();
            return false;
        }

        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);
        if (CheckFramebuffer("scene-resolve") != GL_FRAMEBUFFER_COMPLETE) {
            Destroy();
            return false;
        }
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);
        if (CheckFramebuffer("scene-single-sample") != GL_FRAMEBUFFER_COMPLETE) {
            Destroy();
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    FR_INFO("Scene target %dx%d, %dx MSAA, %s.", m_width, m_height, m_samples, m_hdr ? "HDR" : "LDR");
    return true;
}

void SceneTarget::Destroy() {
    if (m_resolveFbo) {
        glDeleteFramebuffers(1, &m_resolveFbo);
        m_resolveFbo = 0;
    }
    if (m_drawFbo) {
        glDeleteFramebuffers(1, &m_drawFbo);
        m_drawFbo = 0;
    }
    if (m_colorTexture) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthTexture) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    if (m_colorRenderbuffer) {
        glDeleteRenderbuffers(1, &m_colorRenderbuffer);
        m_colorRenderbuffer = 0;
    }
    if (m_depthRenderbuffer) {
        glDeleteRenderbuffers(1, &m_depthRenderbuffer);
        m_depthRenderbuffer = 0;
    }
    m_width = m_height = 0;
}

bool SceneTarget::Resize(int width, int height) {
    if (width == m_width && height == m_height) {
        return true;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    const int samples = m_samples;
    const bool hdr = m_hdr;
    Destroy();
    return Create(width, height, samples, hdr);
}

void SceneTarget::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_drawFbo);
    glViewport(0, 0, m_width, m_height);
}

void SceneTarget::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneTarget::Resolve() const {
    if (!IsMultisampled() || m_resolveFbo == 0) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_drawFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    // Colour for the post pass, depth for the outline (both are multisampled,
    // so they have to be resolved before they can be sampled).
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ShadowMap::~ShadowMap() {
    Destroy();
}

bool ShadowMap::Create(int size) {
    Destroy();

    // Never ask for more than the driver can allocate.
    GLint maxTextureSize = 8192;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_size = std::clamp(size, 256, std::max(256, std::min(8192, static_cast<int>(maxTextureSize))));
    if (m_size != size) {
        FR_WARN("Shadow map size %d clamped to %d (driver limit %d).", size, m_size, maxTextureSize);
    }

    for (int cascade = 0; cascade < kCascades; ++cascade) {
        glGenTextures(1, &m_depthTextures[cascade]);
        glBindTexture(GL_TEXTURE_2D, m_depthTextures[cascade]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_size, m_size, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        // The shaders sample the depth texture as a regular sampler2D and compare
        // manually. A depth texture with compare mode NONE is only
        // texture-complete with NEAREST filtering -- LINEAR would make the
        // texture incomplete on strict drivers and sampling would return 0.0,
        // which looks like "everything is in shadow".
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTextures[0], 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (CheckFramebuffer("shadow-map") != GL_FRAMEBUFFER_COMPLETE) {
        Destroy();
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    FR_INFO("Shadow maps created: %d cascades at %dx%d.", kCascades, m_size, m_size);
    return true;
}

void ShadowMap::Destroy() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    for (int cascade = 0; cascade < kCascades; ++cascade) {
        if (m_depthTextures[cascade]) {
            glDeleteTextures(1, &m_depthTextures[cascade]);
            m_depthTextures[cascade] = 0;
        }
    }
    m_size = 0;
}

bool ShadowMap::Resize(int size) {
    if (size == m_size) {
        return true;
    }
    return Create(size);
}

void ShadowMap::Bind(int cascade) const {
    if (cascade < 0 || cascade >= kCascades) {
        cascade = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           m_depthTextures[cascade], 0);
    glViewport(0, 0, m_size, m_size);
}

void ShadowMap::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

mat4 ShadowMap::ComputeLightMatrix(int cascade, const vec3& sunDirection, const vec3& cameraPosition,
                                   const vec3& cameraForward, float radius, float depth) const {
    (void)cascade;
    const vec3 dir = glm::normalize(sunDirection);
    const vec3 center = cameraPosition + cameraForward * radius * 0.45f;

    // Eye placed opposite the light direction so the ortho box encloses the view.
    const vec3 eye = center - dir * (depth * 0.5f + radius);
    vec3 up(0, 1, 0);
    if (std::fabs(glm::dot(dir, up)) > 0.98f) {
        up = vec3(1, 0, 0);
    }
    mat4 lightView = glm::lookAt(eye, center, up);
    mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.1f, depth + radius * 2.0f);
    mat4 lightSpace = lightProj * lightView;

    // Snap the light-space origin to whole texels to stop shadow swimming.
    const vec4 originInLight = lightSpace * vec4(center, 1.0f);
    const float texelSize = 2.0f / static_cast<float>(std::max(1, m_size));
    const float snapX = std::floor(originInLight.x / texelSize) * texelSize - originInLight.x;
    const float snapY = std::floor(originInLight.y / texelSize) * texelSize - originInLight.y;
    lightSpace = glm::translate(mat4(1.0f), vec3(snapX, snapY, 0.0f)) * lightSpace;
    return lightSpace;
}

BloomChain::~BloomChain() {
    Destroy();
}

bool BloomChain::Create(int width, int height) {
    Destroy();
    m_width = std::max(1, width / 2);
    m_height = std::max(1, height / 2);

    m_colorTextureA = CreateColorTexture(m_width, m_height, true);
    m_colorTextureB = CreateColorTexture(m_width, m_height, true);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTextureA, 0);
    if (CheckFramebuffer("bloom") != GL_FRAMEBUFFER_COMPLETE) {
        Destroy();
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    FR_INFO("Bloom chain %dx%d.", m_width, m_height);
    return true;
}

void BloomChain::Destroy() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_colorTextureA) {
        glDeleteTextures(1, &m_colorTextureA);
        m_colorTextureA = 0;
    }
    if (m_colorTextureB) {
        glDeleteTextures(1, &m_colorTextureB);
        m_colorTextureB = 0;
    }
    m_width = m_height = 0;
}

bool BloomChain::Resize(int width, int height) {
    const int w = std::max(1, width / 2);
    const int h = std::max(1, height / 2);
    if (w == m_width && h == m_height) {
        return true;
    }
    Destroy();
    return Create(width, height);
}

void BloomChain::BindTarget(int index) const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           index == 0 ? m_colorTextureA : m_colorTextureB, 0);
    glViewport(0, 0, m_width, m_height);
}

void BloomChain::Unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace frontier::render
