#version 450 core

// Vertex stage for imported mesh assets (OBJ). Unlike the built-in primitives
// these are drawn one at a time with an explicit model matrix, because each
// asset has its own vertex layout and triangle count.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform vec3 uColor;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vAlbedo;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV;
    vAlbedo = aColor.rgb * uColor;
    gl_Position = uViewProj * world;
}
