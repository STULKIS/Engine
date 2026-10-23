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

// --- Stylised (NPR) outline ------------------------------------------------
uniform sampler2D uDepthTexture;   // resolved scene depth
uniform vec2 uInvResolution;       // 1 / render target size
uniform int uOutlineEnabled;
uniform float uOutlineThickness;   // pixels
uniform float uOutlineStrength;    // 0..1
uniform vec3 uOutlineColor;
uniform float uNearPlane;
uniform float uFarPlane;
uniform int uCelShading;
uniform float uSaturation;
uniform int uPosterizeLevels;      // 0 = off

vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Eye-space distance for a depth-buffer sample.
float LinearEyeDepth(vec2 uv) {
    float depth = texture(uDepthTexture, uv).r;
    if (depth >= 0.99999) {
        return uFarPlane * 8.0; // sky / cleared depth
    }
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) /
           (uFarPlane + uNearPlane - ndc * (uFarPlane - uNearPlane));
}

// Silhouette detection from depth discontinuities. Comparing relative depth
// jumps keeps the line width consistent from 1 m to 800 m, which is what makes
// hand-drawn outlines look uniform.
float SilhouetteEdge(vec2 uv) {
    vec2 texel = uInvResolution * max(uOutlineThickness, 0.5);
    float center = LinearEyeDepth(uv);
    float edge = 0.0;
    vec2 offsets[8] = vec2[8](
        vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 1.0,  1.0), vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2(-1.0, -1.0));
    for (int i = 0; i < 8; ++i) {
        float neighbour = LinearEyeDepth(uv + offsets[i] * texel);
        float jump = (neighbour - center) / max(center, 0.35);
        edge = max(edge, smoothstep(0.06, 0.14, jump));
    }
    return edge;
}

void main() {
    vec3 color = texture(uSceneTexture, vUV).rgb;
    if (uUseBloom == 1) {
        color += texture(uBloomTexture, vUV).rgb * uBloomStrength;
    }
    color *= uExposure;

    if (uCelShading == 1) {
        // Softer shoulder than ACES: flat colour fields keep their saturation,
        // which is what makes the cel bands read as paint rather than plastic.
        color = color / (1.0 + color * 0.62);
        color = clamp(color * 1.15, 0.0, 1.0);
    } else if (uUseTonemap == 1) {
        color = ACESFilmic(color);
    } else {
        color = clamp(color, 0.0, 1.0);
    }

    // Ink the silhouettes over the shaded image (after tone mapping, so the
    // lines stay dark and crisp instead of being crushed by the shoulder).
    if (uOutlineEnabled == 1) {
        float edge = SilhouetteEdge(vUV);
        color = mix(color, uOutlineColor, edge * uOutlineStrength);
    }

    // Colour grade: saturation (and, for the stylised path, optional posterise).
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, uSaturation);
    vec2 centered = vUV * 2.0 - 1.0;
    float vignette = 1.0 - dot(centered, centered) * uVignette;
    color *= clamp(vignette, 0.0, 1.0);

    // Manual sRGB encoding (the default framebuffer is not sRGB-enabled).
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));

    // Optional hard quantisation of the final image for a graphic look.
    if (uPosterizeLevels > 1) {
        float levels = float(uPosterizeLevels);
        color = floor(color * levels + 0.5) / levels;
    }

    oColor = vec4(color, 1.0);
}
