#include "core/Noise.h"

#include <algorithm>
#include <cmath>

namespace derecho::core {
namespace {

inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float Lerp01(float a, float b, float t) { return a + (b - a) * t; }
inline int32_t FloorToInt(float v) { return static_cast<int32_t>(std::floor(v)); }

inline uint32_t MixBits(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

} // namespace

ValueNoise::ValueNoise(uint32_t seed) : m_seed(MixBits(seed + 0x9E3779B9u)) {}

float ValueNoise::HashToFloat(uint32_t h) {
    // Map to [-1,1]
    return static_cast<float>(h & 0x00FFFFFFu) / 8388608.0f - 1.0f;
}

uint32_t ValueNoise::Hash2(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= seed * 0xcb1ab31fu;
    return MixBits(h);
}

uint32_t ValueNoise::Hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= static_cast<uint32_t>(z) * 0x2f5d1e37u;
    h ^= seed * 0xcb1ab31fu;
    return MixBits(h);
}

float ValueNoise::Sample2D(float x, float y) const {
    const int32_t x0 = FloorToInt(x);
    const int32_t y0 = FloorToInt(y);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float u = Fade(fx);
    const float v = Fade(fy);

    const float n00 = HashToFloat(Hash2(x0, y0, m_seed));
    const float n10 = HashToFloat(Hash2(x0 + 1, y0, m_seed));
    const float n01 = HashToFloat(Hash2(x0, y0 + 1, m_seed));
    const float n11 = HashToFloat(Hash2(x0 + 1, y0 + 1, m_seed));

    return Lerp01(Lerp01(n00, n10, u), Lerp01(n01, n11, u), v);
}

float ValueNoise::Sample3D(float x, float y, float z) const {
    const int32_t x0 = FloorToInt(x);
    const int32_t y0 = FloorToInt(y);
    const int32_t z0 = FloorToInt(z);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float fz = z - static_cast<float>(z0);
    const float u = Fade(fx);
    const float v = Fade(fy);
    const float w = Fade(fz);

    const float c000 = HashToFloat(Hash3(x0, y0, z0, m_seed));
    const float c100 = HashToFloat(Hash3(x0 + 1, y0, z0, m_seed));
    const float c010 = HashToFloat(Hash3(x0, y0 + 1, z0, m_seed));
    const float c110 = HashToFloat(Hash3(x0 + 1, y0 + 1, z0, m_seed));
    const float c001 = HashToFloat(Hash3(x0, y0, z0 + 1, m_seed));
    const float c101 = HashToFloat(Hash3(x0 + 1, y0, z0 + 1, m_seed));
    const float c011 = HashToFloat(Hash3(x0, y0 + 1, z0 + 1, m_seed));
    const float c111 = HashToFloat(Hash3(x0 + 1, y0 + 1, z0 + 1, m_seed));

    const float x00 = Lerp01(c000, c100, u);
    const float x10 = Lerp01(c010, c110, u);
    const float x01 = Lerp01(c001, c101, u);
    const float x11 = Lerp01(c011, c111, u);
    return Lerp01(Lerp01(x00, x10, v), Lerp01(x01, x11, v), w);
}

GradientNoise::GradientNoise(uint32_t seed) : m_seed(MixBits(seed + 0x85ebca6bu)) {}

vec2 GradientNoise::Gradient(int32_t x, int32_t y) const {
    const uint32_t h = ValueNoise::Hash2(x, y, m_seed);
    const float angle = static_cast<float>(h & 0xFFFFu) * (6.28318530718f / 65536.0f);
    return vec2(std::cos(angle), std::sin(angle));
}

float GradientNoise::Sample2D(float x, float y) const {
    const int32_t x0 = FloorToInt(x);
    const int32_t y0 = FloorToInt(y);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const vec2 g00 = Gradient(x0, y0);
    const vec2 g10 = Gradient(x0 + 1, y0);
    const vec2 g01 = Gradient(x0, y0 + 1);
    const vec2 g11 = Gradient(x0 + 1, y0 + 1);

    const float n00 = g00.x * fx + g00.y * fy;
    const float n10 = g10.x * (fx - 1.0f) + g10.y * fy;
    const float n01 = g01.x * fx + g01.y * (fy - 1.0f);
    const float n11 = g11.x * (fx - 1.0f) + g11.y * (fy - 1.0f);

    const float u = Fade(fx);
    const float v = Fade(fy);
    // Scale into roughly [-1,1].
    return 1.4f * Lerp01(Lerp01(n00, n10, u), Lerp01(n01, n11, u), v);
}

FractalNoise::FractalNoise(uint32_t seed, int octaves, float lacunarity, float gain)
    : m_noise(seed), m_octaves(octaves), m_lacunarity(lacunarity), m_gain(gain) {
    Random rng(seed * 2654435761u + 17u);
    const float rotation = rng.Range(0.0f, kTwoPi);
    m_rotationCos = std::cos(rotation);
    m_rotationSin = std::sin(rotation);
}

float FractalNoise::FBM(float x, float y) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < m_octaves; ++i) {
        sum += m_noise.Sample2D(x * frequency, y * frequency) * amplitude;
        norm += amplitude;
        amplitude *= m_gain;
        frequency *= m_lacunarity;
        // Rotate the domain a little per octave to hide grid alignment.
        const float nx = x * m_rotationCos - y * m_rotationSin;
        const float ny = x * m_rotationSin + y * m_rotationCos;
        x = nx;
        y = ny;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float FractalNoise::Ridged(float x, float y) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < m_octaves; ++i) {
        const float n = 1.0f - std::fabs(m_noise.Sample2D(x * frequency, y * frequency));
        sum += n * n * amplitude;
        norm += amplitude;
        amplitude *= m_gain;
        frequency *= m_lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float FractalNoise::Billow(float x, float y) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < m_octaves; ++i) {
        sum += std::fabs(m_noise.Sample2D(x * frequency, y * frequency)) * amplitude;
        norm += amplitude;
        amplitude *= m_gain;
        frequency *= m_lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

float FractalNoise::Warped(float x, float y) const {
    const float wx = FBM(x + 5.2f, y + 1.3f) * 1.8f;
    const float wy = FBM(x - 3.7f, y + 8.1f) * 1.8f;
    return FBM(x + wx, y + wy);
}

float Cellular2D(float x, float y, uint32_t seed) {
    const int32_t xi = FloorToInt(x);
    const int32_t yi = FloorToInt(y);
    float best = 1e9f;
    for (int32_t oy = -1; oy <= 1; ++oy) {
        for (int32_t ox = -1; ox <= 1; ++ox) {
            const uint32_t h = ValueNoise::Hash2(xi + ox, yi + oy, seed ^ 0x51ed270bu);
            const float px = static_cast<float>(xi + ox) + static_cast<float>(h & 0xFFu) / 255.0f;
            const float py = static_cast<float>(yi + oy) + static_cast<float>((h >> 8) & 0xFFu) / 255.0f;
            const float dx = px - x;
            const float dy = py - y;
            best = std::min(best, dx * dx + dy * dy);
        }
    }
    return std::sqrt(best);
}

} // namespace derecho::core
