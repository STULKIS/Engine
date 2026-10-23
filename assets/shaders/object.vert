#version 450 core

// Vertex stage for scene entities (built-in primitives). Uses the same
// per-instance layout as the foliage instancing path: one draw call per
// primitive type, one transform per entity.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

layout(location = 4) in vec4 aInstColumn0;
layout(location = 5) in vec4 aInstColumn1;
layout(location = 6) in vec4 aInstColumn2;
layout(location = 7) in vec4 aInstColumn3;
layout(location = 8) in vec4 aInstTint;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vAlbedo;

void main() {
    mat4 instanceTransform = mat4(aInstColumn0, aInstColumn1, aInstColumn2, aInstColumn3);
    vec4 world = instanceTransform * vec4(aPosition, 1.0);

    vWorldPos = world.xyz;
    vNormal = normalize(mat3(instanceTransform) * aNormal);
    vUV = aUV;
    // Vertex tint (from the generator) times the entity colour.
    vAlbedo = aColor.rgb * aInstTint.rgb;

    gl_Position = uViewProj * world;
}
