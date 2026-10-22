#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

// Per-instance attributes.
layout(location = 4) in vec4 aInstColumn0;
layout(location = 5) in vec4 aInstColumn1;
layout(location = 6) in vec4 aInstColumn2;
layout(location = 7) in vec4 aInstColumn3;
layout(location = 8) in vec4 aInstTint;

uniform mat4 uViewProj;
uniform float uTime;
uniform vec3 uWindDirection;
uniform float uWindStrength;
uniform int uWindEnabled;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vAlbedo;
out float vWindWeight;

void main() {
    mat4 instanceTransform = mat4(aInstColumn0, aInstColumn1, aInstColumn2, aInstColumn3);
    vec4 world = instanceTransform * vec4(aPosition, 1.0);
    vec3 normal = mat3(instanceTransform) * aNormal;

    // Vertex alpha is the sway weight (0 at the trunk/blade base, 1 at the tip),
    // instance alpha stores a per-plant phase offset.
    float weight = aColor.a;
    vWindWeight = weight;
    if (uWindEnabled == 1 && weight > 0.001) {
        float phase = aInstTint.a;
        vec3 origin = aInstColumn3.xyz;
        float sway = sin(uTime * 1.6 + phase + origin.x * 0.09 + origin.z * 0.13);
        float gust = 0.55 + 0.45 * sin(uTime * 0.31 + origin.x * 0.013 + origin.z * 0.021);
        vec3 offset = uWindDirection * (sway * 0.55 + (gust - 0.5) * 0.9) * uWindStrength * weight;
        // Slight vertical flutter makes the grass read better at close range.
        offset.y += sin(uTime * 4.1 + phase * 2.0 + origin.x) * uWindStrength * weight * 0.18;
        world.xyz += offset;
        normal = normalize(normal + vec3(offset.x, 0.0, offset.z) * 0.35);
    }

    vWorldPos = world.xyz;
    vNormal = normal;
    vUV = aUV;
    vAlbedo = vec4(aColor.rgb * aInstTint.rgb, aColor.a);
    gl_Position = uViewProj * world;
}
