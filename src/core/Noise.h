// Frontier Engine - original procedural noise utilities (value/simplex style,
// gradient noise, fractal Brownian motion, ridged and billowed variants).
#pragma once

#include "math/Math.h"

#include <cstdint>

namespace frontier::core {

// Small, fast, deterministic PRNG. Not cryptographic; used for world generation.
class Random {
public:
    explicit Random(uint32_t seed = 1337u) : m_state(seed ? seed : 0x9E3779B9u) {}

    uint32_t NextU32() {
        // xorshift32
        uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_state = x;
        return x;
    }

    float NextFloat() { return static_cast<float>(NextU32() & 0x00FFFFFFu) / 16777216.0f; }
    float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
    int RangeInt(int lo, int hiInclusive) {
        if (hiInclusive <= lo) return lo;
        return lo + static_cast<int>(NextU32() % static_cast<uint32_t>(hiInclusive - lo + 1));
    }
    bool Chance(float probability) { return NextFloat() < probability; }

private:
    uint32_t m_state;
};

// Hash based value noise with a smooth quintic interpolation. Deterministic for
// a given (seed, x, y, z) and independent of evaluation order.
class ValueNoise {
public:
    explicit ValueNoise(uint32_t seed = 0u);

    float Sample2D(float x, float y) const;
    float Sample3D(float x, float y, float z) const;

    // Deterministic integer hashes shared by the noise and scattering code.
    static uint32_t Hash2(int32_t x, int32_t y, uint32_t seed);
    static uint32_t Hash3(int32_t x, int32_t y, int32_t z, uint32_t seed);

private:
    static float HashToFloat(uint32_t h);
    uint32_t m_seed;
};

// Gradient (Perlin style) noise, optionally rotated per octave.
class GradientNoise {
public:
    explicit GradientNoise(uint32_t seed = 0u);

    float Sample2D(float x, float y) const;

private:
    vec2 Gradient(int32_t x, int32_t y) const;
    uint32_t m_seed;
};

// Fractal composition of gradient noise. All methods are seed-driven and stable.
class FractalNoise {
public:
    explicit FractalNoise(uint32_t seed = 0u, int octaves = 5, float lacunarity = 2.0f, float gain = 0.5f);

    float FBM(float x, float y) const;      // [-1,1]-ish, smooth hills
    float Ridged(float x, float y) const;   // [0,1], sharp mountain ridges
    float Billow(float x, float y) const;   // [0,1], puffy cloud-like
    float Warped(float x, float y) const;   // domain-warped FBM, eroded terrain look

    int Octaves() const { return m_octaves; }

private:
    GradientNoise m_noise;
    int m_octaves;
    float m_lacunarity;
    float m_gain;
    float m_rotationCos = 0.8f;
    float m_rotationSin = 0.6f;
};

// Cheap tileable cellular function used for grass clump masks and rock detail.
float Cellular2D(float x, float y, uint32_t seed = 0u);

} // namespace frontier::core
