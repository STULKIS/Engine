#version 450 core

in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uSceneTexture;
uniform sampler2D uBloomTexture;
uniform float uExposure;
uniform float uBloomStrength;
uniform int uUseBloom;
uniform int uUseTonemap;
uniform float uVignette;

vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uSceneTexture, vUV).rgb;
    if (uUseBloom == 1) {
        color += texture(uBloomTexture, vUV).rgb * uBloomStrength;
    }
    color *= uExposure;

    if (uUseTonemap == 1) {
        color = ACESFilmic(color);
    } else {
        color = clamp(color, 0.0, 1.0);
    }

    // Gentle filmic grade: slight desaturation of highlights and a vignette.
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, 1.06);
    vec2 centered = vUV * 2.0 - 1.0;
    float vignette = 1.0 - dot(centered, centered) * uVignette;
    color *= clamp(vignette, 0.0, 1.0);

    // Manual sRGB encoding (the default framebuffer is not sRGB-enabled).
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));
    oColor = vec4(color, 1.0);
}
