#version 450 core

in vec2 vUV;
layout(location = 0) out vec4 oColor;

uniform sampler2D uInputTexture;
uniform vec2 uDirection;   // texel-sized step (horizontal or vertical)

void main() {
    // 9-tap Gaussian.
    float weights[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(uInputTexture, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * float(i);
        result += texture(uInputTexture, vUV + offset).rgb * weights[i];
        result += texture(uInputTexture, vUV - offset).rgb * weights[i];
    }
    oColor = vec4(result, 1.0);
}
