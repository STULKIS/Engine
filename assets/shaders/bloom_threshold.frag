#version 450 core

in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uSceneTexture;
uniform float uThreshold;

void main() {
    vec3 color = texture(uSceneTexture, vUV).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = 0.35;
    float contribution = max(luminance - uThreshold + knee, 0.0);
    contribution = min(contribution, 2.0 * knee);
    contribution = contribution * contribution / (4.0 * knee + 0.0001);
    float weight = max(contribution, luminance - uThreshold) / max(luminance, 0.0001);
    oColor = vec4(color * weight, 1.0);
}
