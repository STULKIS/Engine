#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;
uniform float uTime;
uniform vec3 uCameraPos;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vWaveHeight;

// Three overlapping directional waves. The analytic derivative gives a normal
// without needing a normal map.
const int WAVE_COUNT = 3;

void main() {
    vec3 world = aPosition;

    // Wave parameters: direction, wavelength, amplitude, speed.
    vec2 directions[3] = vec2[3](
        normalize(vec2(0.86, 0.5)),
        normalize(vec2(-0.42, 0.91)),
        normalize(vec2(0.62, -0.78))
    );
    float wavelengths[3] = float[3](42.0, 23.0, 11.0);
    float amplitudes[3] = float[3](0.42, 0.24, 0.11);
    float speeds[3] = float[3](0.9, 1.35, 2.1);

    float height = 0.0;
    vec2 slope = vec2(0.0);
    for (int i = 0; i < WAVE_COUNT; ++i) {
        vec2 dir = directions[i];
        float k = 6.2831853 / wavelengths[i];
        float phase = dot(dir, world.xz) * k + uTime * speeds[i];
        height += sin(phase) * amplitudes[i];
        slope += dir * cos(phase) * amplitudes[i] * k;
    }

    world.y += height;
    vWaveHeight = height;

    // Normal from the height gradient (scale down for a calmer surface).
    vec3 normal = normalize(vec3(-slope.x * 1.6, 1.0, -slope.y * 1.6));
    vNormal = normal;
    vWorldPos = world;
    vUV = aUV;
    gl_Position = uViewProj * vec4(world, 1.0);
}
