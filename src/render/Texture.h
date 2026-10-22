// Frontier Engine - 2D textures including procedural noise/detail generation.
#pragma once

#include "render/GL.h"

#include <cstdint>
#include <string>
#include <vector>

namespace frontier::render {

struct TextureDesc {
    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    bool generateMips = true;
    bool repeat = true;
    bool anisoFriendly = true;
};

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Uploads tightly packed RGBA8 data.
    bool CreateRGBA8(int width, int height, const std::vector<uint8_t>& pixels,
                     bool srgb, const TextureDesc& desc = {});
    // Uploads floating point RGBA data (used for HDR lookup tables).
    bool CreateRGBA32F(int width, int height, const std::vector<float>& pixels, const TextureDesc& desc = {});
    // Uploads single channel 32-bit float data (terrain heightfield for water).
    bool CreateR32F(int width, int height, const std::vector<float>& pixels, bool repeat = false);
    // Creates an empty texture usable as a render target attachment.
    bool CreateRenderTarget(int width, int height, GLenum internalFormat, GLenum format, GLenum type);

    void Destroy();

    void Bind(int unit) const;
    GLuint Id() const { return m_texture; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    bool Valid() const { return m_texture != 0; }
    const std::string& Name() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

private:
    GLuint m_texture = 0;
    int m_width = 0;
    int m_height = 0;
    std::string m_name;
};

// Procedural texture generation used for terrain detail and foliage albedo.
// All output is generated with the engine's own noise, so no third-party image
// assets are required at runtime.
namespace procedural {

// 512x512 moss/grass detail map (RGBA): R grass, G dry grass, B soil, A variation.
std::vector<uint8_t> GrassDetail(int size, uint32_t seed);

// 512x512 rock detail map with subtle crack structure.
std::vector<uint8_t> RockDetail(int size, uint32_t seed);

// 512x512 generic ground/soil map.
std::vector<uint8_t> SoilDetail(int size, uint32_t seed);

// Normal map derived from a height function baked into a grayscale image.
std::vector<uint8_t> HeightToNormal(const std::vector<uint8_t>& height, int size, float strength);

// Vertical gradient lookup used by the sky and fog (RGB float, sRGB-independent).
std::vector<float> SkyGradientLUT(int width);

} // namespace procedural
} // namespace frontier::render
