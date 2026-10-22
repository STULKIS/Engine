#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;

out vec2 vNDC;
out vec2 vUV;

void main() {
    vNDC = aPosition.xy;
    vUV = aUV;
    gl_Position = vec4(aPosition.xy, 1.0, 1.0);
}
