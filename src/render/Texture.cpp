#include "render/Texture.h"

#include "core/Log.h"
#include "core/Noise.h"
#include "math/Math.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace derecho::render {

Texture::~Texture() {
    Destroy();
}

Texture::Texture(Texture&& other) noexcept
    : m_texture(other.m_texture), m_width(other.m_width), m_height(other.m_height), m_name(std::move(other.m_name)) {
    other.m_texture = 0;
    other.m_width = 0;
    other.m_height = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_texture = other.m_texture;
        m_width = other.m_width;
        m_height = other.m_height;
        m_name = std::move(other.m_name);
        other.m_texture = 0;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

void Texture::Destroy() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_width = m_height = 0;
}

void Texture::Bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

bool Texture::CreateRGBA8(int width, int height, const std::vector<uint8_t>& pixels, bool srgb,
                          const TextureDesc& desc) {
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height * 4) {
        DE_ERROR("Texture::CreateRGBA8 invalid data (%dx%d, %zu bytes)", width, height, pixels.size());
        return false;
    }
    Destroy();
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const GLenum internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, desc.generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (desc.generateMips) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    m_width = width;
    m_height = height;
    return true;
}

bool Texture::CreateRGBA32F(int width, int height, const std::vector<float>& pixels, const TextureDesc& desc) {
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height * 4) {
        DE_ERROR("Texture::CreateRGBA32F invalid data (%dx%d, %zu floats)", width, height, pixels.size());
        return false;
    }
    Destroy();
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, desc.generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (desc.generateMips) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    m_width = width;
    m_height = height;
    return true;
}

bool Texture::CreateR32F(int width, int height, const std::vector<float>& pixels, bool repeat) {
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height) {
        DE_ERROR("Texture::CreateR32F invalid data (%dx%d, %zu floats)", width, height, pixels.size());
        return false;
    }
    Destroy();
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_width = width;
    m_height = height;
    return true;
}

bool Texture::CreateRenderTarget(int width, int height, GLenum internalFormat, GLenum format, GLenum type) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    Destroy();
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_width = width;
    m_height = height;
    return true;
}

namespace procedural {
namespace {

inline uint8_t ToByte(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

std::vector<uint8_t> GrassDetail(int size, uint32_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    core::FractalNoise fine(seed + 11u, 5, 2.1f, 0.55f);
    core::FractalNoise macro(seed + 29u, 4, 2.0f, 0.5f);
    core::ValueNoise blades(seed + 73u);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            // Tileable coordinates.
            const float angle = kTwoPi * u;
            const float px = std::cos(angle) * 5.0f;
            const float py = std::sin(angle) * 5.0f;
            const float ring = 5.0f * kTwoPi;

            const float n = fine.FBM(px * 3.0f + ring * 0.0f, py * 3.0f) * 0.5f + 0.5f;
            const float m = macro.FBM(px * 0.6f, py * 0.6f + v * 2.0f) * 0.5f + 0.5f;
            const float streak = blades.Sample2D(u * 220.0f, v * 18.0f) * 0.5f + 0.5f;
            const float clump = core::Cellular2D(u * 18.0f, v * 18.0f, seed) * 0.7f;

            const float green = Saturate(0.55f + (n - 0.5f) * 0.5f + (streak - 0.5f) * 0.28f);
            const float dry = Saturate(0.35f + (m - 0.5f) * 0.8f + (1.0f - clump) * 0.25f);
            const float soil = Saturate(0.2f + (1.0f - green) * 0.6f + (m - 0.5f) * 0.4f);
            const float variation = Saturate(0.4f + (n - 0.5f) * 0.9f);

            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            pixels[i + 0] = ToByte(green);
            pixels[i + 1] = ToByte(dry);
            pixels[i + 2] = ToByte(soil);
            pixels[i + 3] = ToByte(variation);
        }
    }
    return pixels;
}

std::vector<uint8_t> RockDetail(int size, uint32_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    core::FractalNoise base(seed + 5u, 6, 2.2f, 0.52f);
    core::FractalNoise cracks(seed + 19u, 5, 2.6f, 0.45f);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            const float wrapped = core::Cellular2D(u * 7.0f, v * 7.0f, seed);
            const float layers = base.Billow(u * 9.0f, v * 4.0f);
            const float crack = cracks.Ridged(u * 14.0f, v * 14.0f);

            const float tone = Saturate(0.42f + (layers - 0.5f) * 0.5f - (wrapped - 0.7f) * 0.25f);
            const float dark = Saturate(1.0f - crack * 0.8f);
            const float warm = Saturate(0.35f + wrapped * 0.5f);

            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            pixels[i + 0] = ToByte(tone * 1.02f);
            pixels[i + 1] = ToByte(tone * 0.98f);
            pixels[i + 2] = ToByte(tone * 0.92f);
            pixels[i + 3] = ToByte(Saturate(dark * 0.8f + warm * 0.2f));
        }
    }
    return pixels;
}

std::vector<uint8_t> SoilDetail(int size, uint32_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    core::FractalNoise soil(seed + 3u, 5, 2.0f, 0.55f);
    core::ValueNoise grain(seed + 91u);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            const float n = soil.FBM(u * 12.0f, v * 12.0f) * 0.5f + 0.5f;
            const float g = grain.Sample2D(u * 180.0f, v * 180.0f) * 0.5f + 0.5f;
            const float pebble = Saturate(core::Cellular2D(u * 26.0f, v * 26.0f, seed) * 1.6f - 0.3f);

            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            pixels[i + 0] = ToByte(Saturate(0.45f + n * 0.35f - pebble * 0.2f));
            pixels[i + 1] = ToByte(Saturate(0.34f + n * 0.3f + g * 0.08f));
            pixels[i + 2] = ToByte(Saturate(0.24f + n * 0.22f - pebble * 0.12f));
            pixels[i + 3] = ToByte(Saturate(g * 0.6f + pebble * 0.4f));
        }
    }
    return pixels;
}

std::vector<uint8_t> HeightToNormal(const std::vector<uint8_t>& height, int size, float strength) {
    std::vector<uint8_t> normal(static_cast<size_t>(size) * size * 4);
    auto sample = [&](int x, int y) {
        x = (x + size) % size;
        y = (y + size) % size;
        return static_cast<float>(height[(static_cast<size_t>(y) * size + x) * 4]) / 255.0f;
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (sample(x + 1, y) - sample(x - 1, y)) * strength;
            const float dy = (sample(x, y + 1) - sample(x, y - 1)) * strength;
            vec3 n = glm::normalize(vec3(-dx, -dy, 1.0f));
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            normal[i + 0] = ToByte(n.x * 0.5f + 0.5f);
            normal[i + 1] = ToByte(n.y * 0.5f + 0.5f);
            normal[i + 2] = ToByte(n.z * 0.5f + 0.5f);
            normal[i + 3] = 255;
        }
    }
    return normal;
}

std::vector<float> SkyGradientLUT(int width) {
    std::vector<float> lut(static_cast<size_t>(width) * 4);
    for (int i = 0; i < width; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(width - 1);
        // Deep blue zenith to pale horizon; the shader tints this by sun colour.
        const vec3 zenith(0.09f, 0.22f, 0.5f);
        const vec3 horizon(0.62f, 0.74f, 0.88f);
        const vec3 c = Lerp(horizon, zenith, std::pow(t, 0.55f));
        lut[static_cast<size_t>(i) * 4 + 0] = c.r;
        lut[static_cast<size_t>(i) * 4 + 1] = c.g;
        lut[static_cast<size_t>(i) * 4 + 2] = c.b;
        lut[static_cast<size_t>(i) * 4 + 3] = 1.0f;
    }
    return lut;
}

} // namespace procedural
} // namespace derecho::render
