#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;

void main() {
    vWorldPos = aPosition;
    vNormal = aNormal;
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
