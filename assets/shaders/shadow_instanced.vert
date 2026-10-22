#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec4 aColor;      // alpha = wind weight
layout(location = 4) in vec4 aInstColumn0;
layout(location = 5) in vec4 aInstColumn1;
layout(location = 6) in vec4 aInstColumn2;
layout(location = 7) in vec4 aInstColumn3;
layout(location = 8) in vec4 aInstTint;

uniform mat4 uLightSpaceMatrix;
uniform float uTime;
uniform vec3 uWindDirection;
uniform float uWindStrength;

void main() {
    mat4 instanceTransform = mat4(aInstColumn0, aInstColumn1, aInstColumn2, aInstColumn3);
    vec4 world = instanceTransform * vec4(aPosition, 1.0);

    float weight = aColor.a;
    if (weight > 0.001) {
        vec3 origin = aInstColumn3.xyz;
        float phase = aInstTint.a;
        float sway = sin(uTime * 1.6 + phase + origin.x * 0.09 + origin.z * 0.13);
        float gust = 0.55 + 0.45 * sin(uTime * 0.31 + origin.x * 0.013 + origin.z * 0.021);
        world.xyz += uWindDirection * (sway * 0.55 + (gust - 0.5) * 0.9) * uWindStrength * weight;
    }

    gl_Position = uLightSpaceMatrix * world;
}
